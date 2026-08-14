#pragma once

// Cycle clock. This header is the authority on the probe sequence:
//
//   open:  lfence; rdtsc; lfence
//   close: rdtscp; lfence
//
// both wrapped in empty asm memory clobbers so the compiler cannot move the
// measured work across the stamps either. On aarch64 the source is cntvct_el0
// with isb barriers (compiled but so far unverified on real hardware).

#include <cstdint>
#include <string>
#include <vector>

namespace ob::bench {

#if defined(__x86_64__)

inline uint64_t cycle_start() {
    asm volatile("" ::: "memory");
    __builtin_ia32_lfence();
    uint64_t tsc = __builtin_ia32_rdtsc();
    __builtin_ia32_lfence();
    asm volatile("" ::: "memory");
    return tsc;
}

inline uint64_t cycle_end() {
    asm volatile("" ::: "memory");
    unsigned int aux = 0;
    uint64_t tsc = __builtin_ia32_rdtscp(&aux);
    __builtin_ia32_lfence();
    asm volatile("" ::: "memory");
    return tsc;
}

inline const char* cycle_source_name() { return "rdtscp"; }

#elif defined(__aarch64__)

inline uint64_t cycle_start() {
    uint64_t v = 0;
    asm volatile("isb; mrs %0, cntvct_el0" : "=r"(v)::"memory");
    return v;
}

inline uint64_t cycle_end() {
    uint64_t v = 0;
    asm volatile("isb; mrs %0, cntvct_el0" : "=r"(v)::"memory");
    return v;
}

inline const char* cycle_source_name() { return "cntvct_el0"; }

#else
#error "no cycle source for this architecture"
#endif

// Startup qualification per the measurement rules: the clock is publishable
// only when the TSC is invariant. Anything less marks the session dev-only.
struct ClockQualification {
    bool qualified = false;
    std::vector<std::string> problems; // empty when qualified
};

ClockQualification check_clock();

// Cycles -> nanoseconds factor, calibrated against CLOCK_MONOTONIC_RAW.
// Windows under 500 ms are allowed for smoke tests but mark the result
// dev-only; the caller records the window it used.
struct Calibration {
    double cycles_per_ns = 0.0;
    uint64_t window_ns = 0;
};

Calibration calibrate(uint64_t window_ns);

} // namespace ob::bench
