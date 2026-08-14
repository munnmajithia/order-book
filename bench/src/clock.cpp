#include "bench/clock.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

// POSIX spellings on purpose: <ctime> is not specified to declare
// clock_gettime or the CLOCK_* clocks, and include-cleaner rightly wants the
// header that provides them.
#include <time.h> // NOLINT(modernize-deprecated-headers)

namespace ob::bench {

namespace {

uint64_t monotonic_raw_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

#if defined(__x86_64__)
bool cpuinfo_has_flags(std::vector<std::string>& problems) {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.starts_with("flags")) {
            for (const char* flag : {"constant_tsc", "nonstop_tsc", "rdtscp"}) {
                if (line.find(flag) == std::string::npos) {
                    problems.emplace_back(std::string("cpuinfo flag missing: ") + flag);
                }
            }
            return problems.empty();
        }
    }
    problems.emplace_back("could not read cpu flags from /proc/cpuinfo");
    return false;
}
#endif

} // namespace

ClockQualification check_clock() {
    ClockQualification q;
#if defined(__x86_64__)
    q.qualified = cpuinfo_has_flags(q.problems);
#else
    // cntvct_el0 ticks at a fixed platform frequency but this port is
    // unverified on real hardware; never qualified until it is.
    q.problems.emplace_back("aarch64 cycle source is unverified");
    q.qualified = false;
#endif
    return q;
}

Calibration calibrate(uint64_t window_ns) {
    Calibration cal;
    const uint64_t wall_start = monotonic_raw_ns();
    const uint64_t cyc_start = cycle_start();
    uint64_t wall_now = wall_start;
    while (wall_now - wall_start < window_ns) {
        wall_now = monotonic_raw_ns();
    }
    const uint64_t cyc_end = cycle_end();
    cal.window_ns = wall_now - wall_start;
    if (cal.window_ns > 0) {
        cal.cycles_per_ns =
            static_cast<double>(cyc_end - cyc_start) / static_cast<double>(cal.window_ns);
    }
    return cal;
}

} // namespace ob::bench
