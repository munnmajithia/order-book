#pragma once

// Bench result files: JSON with the machine config embedded and an explicit
// PUBLISHABLE / DEV-ONLY stamp. The stamp is computed, never asserted by the
// caller: a result is publishable only when nothing disqualifies it, and the
// disqualifier list travels inside the file.

#include "bench/clock.hpp"
#include "bench/histogram.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ob::bench {

struct MachineConfig {
    std::string cpu_model;
    std::string governor; // "unknown" when sysfs does not expose one
    std::string kernel;
    std::string compiler;
    std::string build_type;
    std::string sanitizer; // empty when none
};

MachineConfig read_machine_config();

struct RunStats {
    uint64_t samples = 0;
    uint64_t anomalies = 0; // probe pairs with end < start; never folded in
    uint64_t p50_ns = 0;
    uint64_t p99_ns = 0;
    uint64_t p999_ns = 0;
};

struct BenchResult {
    std::string name;
    MachineConfig machine;
    std::string cycle_source;
    Calibration calibration;
    ClockQualification clock;
    uint64_t empty_probe_p50_ns = 0; // measurement overhead, reported not subtracted
    std::vector<RunStats> runs;

    // Disqualifiers actually decidable at this layer; the designated-machine
    // rule is mechanical: while no machine is designated, nothing is
    // publishable from anywhere.
    [[nodiscard]] std::vector<std::string> disqualifiers() const;
    [[nodiscard]] bool publishable() const { return disqualifiers().empty(); }
};

// Serialize to JSON on disk. Returns false on I/O failure.
bool write_json(const BenchResult& result, const std::string& path);

// Demo bench measuring a trivial function; fills every field of the result.
// Small calibration windows are for smoke tests and produce dev-only results.
BenchResult run_demo_bench(uint64_t calibration_window_ns);

} // namespace ob::bench
