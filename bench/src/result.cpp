#include "bench/result.hpp"

#include "bench/clock.hpp"
#include "bench/histogram.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ostream>
#include <string>
#include <vector>

#include <sys/utsname.h>

namespace ob::bench {

namespace {

std::string read_cpu_model() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.starts_with("model name")) {
            const auto colon = line.find(':');
            if (colon != std::string::npos && colon + 2 <= line.size()) {
                return line.substr(colon + 2);
            }
        }
    }
    return "unknown";
}

std::string read_governor() {
    std::ifstream f("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    std::string governor;
    if (f >> governor) {
        return governor;
    }
    return "unknown";
}

std::string compiler_id() {
#if defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc ") + __VERSION__;
#else
    return "unknown";
#endif
}

void json_escape_into(std::ostream& out, const std::string& s) {
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            out << '\\' << c;
        } else if (c == '\n') {
            out << "\\n";
        } else {
            out << c;
        }
    }
}

} // namespace

MachineConfig read_machine_config() {
    MachineConfig mc;
    mc.cpu_model = read_cpu_model();
    mc.governor = read_governor();
    utsname uts{};
    mc.kernel = uname(&uts) == 0 ? std::string(static_cast<const char*>(uts.release)) : "unknown";
    mc.compiler = compiler_id();
#ifdef NDEBUG
    mc.build_type = "release";
#else
    mc.build_type = "debug";
#endif
#ifdef OB_SANITIZER_NAME
    mc.sanitizer = OB_SANITIZER_NAME;
#endif
    return mc;
}

std::vector<std::string> BenchResult::disqualifiers() const {
    std::vector<std::string> reasons;
    // No designated machine exists while bench/designated_machine.txt is
    // absent from the repo; every result is dev-only by construction.
#ifndef OB_DESIGNATED_MACHINE
    reasons.emplace_back("no designated bench machine");
#endif
    if (!clock.qualified) {
        for (const auto& p : clock.problems) {
            reasons.push_back("clock: " + p);
        }
    }
    if (calibration.window_ns < 500'000'000ULL) {
        reasons.emplace_back("calibration window under 500 ms");
    }
    if (machine.governor != "performance") {
        reasons.emplace_back("governor not performance");
    }
    if (machine.build_type != "release") {
        reasons.emplace_back("non-release build");
    }
    if (!machine.sanitizer.empty()) {
        reasons.emplace_back("sanitizer build");
    }
    if (runs.size() < 5) {
        reasons.emplace_back("fewer than five runs");
    }
    if (runs.size() >= 2) {
        auto [lo, hi] =
            std::minmax_element(runs.begin(), runs.end(), [](const RunStats& a, const RunStats& b) {
                return a.p50_ns < b.p50_ns;
            });
        std::vector<uint64_t> p50s;
        p50s.reserve(runs.size());
        for (const auto& r : runs) {
            p50s.push_back(r.p50_ns);
        }
        std::nth_element(p50s.begin(), p50s.begin() + static_cast<long>(p50s.size() / 2),
                         p50s.end());
        const uint64_t median = p50s[p50s.size() / 2];
        if (median > 0 &&
            static_cast<double>(hi->p50_ns - lo->p50_ns) > 0.05 * static_cast<double>(median)) {
            reasons.emplace_back("p50 spread across runs over 5%");
        }
    }
    return reasons;
}

bool write_json(const BenchResult& result, const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << "{\n";
    out << R"(  "name": ")";
    json_escape_into(out, result.name);
    out << "\",\n";
    out << R"(  "stamp": ")" << (result.publishable() ? "PUBLISHABLE" : "DEV-ONLY") << "\",\n";
    out << R"(  "disqualifiers": [)";
    const auto reasons = result.disqualifiers();
    for (std::size_t i = 0; i < reasons.size(); ++i) {
        out << (i != 0 ? ", " : "") << '"';
        json_escape_into(out, reasons[i]);
        out << '"';
    }
    out << "],\n";
    out << "  \"machine\": {\n";
    out << R"(    "cpu_model": ")";
    json_escape_into(out, result.machine.cpu_model);
    out << R"(",
    "governor": ")"
        << result.machine.governor << "\",\n";
    out << R"(    "kernel": ")";
    json_escape_into(out, result.machine.kernel);
    out << R"(",
    "compiler": ")";
    json_escape_into(out, result.machine.compiler);
    out << R"(",
    "build_type": ")"
        << result.machine.build_type << "\",\n";
    out << R"(    "sanitizer": ")" << result.machine.sanitizer << "\"\n  },\n";
    out << R"(  "cycle_source": ")" << result.cycle_source << "\",\n";
    out << R"(  "calibration": { "cycles_per_ns": )" << result.calibration.cycles_per_ns
        << R"(, "window_ns": )" << result.calibration.window_ns << " },\n";
    out << R"(  "empty_probe_p50_ns": )" << result.empty_probe_p50_ns << ",\n";
    out << "  \"runs\": [\n";
    for (std::size_t i = 0; i < result.runs.size(); ++i) {
        const auto& r = result.runs[i];
        out << R"(    { "samples": )" << r.samples << R"(, "anomalies": )" << r.anomalies
            << R"(, "p50_ns": )" << r.p50_ns << R"(, "p99_ns": )" << r.p99_ns << R"(, "p999_ns": )"
            << r.p999_ns << " }" << (i + 1 < result.runs.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    return static_cast<bool>(out);
}

namespace {

// The trivial function under test; volatile keeps the work observable.
uint64_t demo_workload() {
    volatile uint64_t acc = 0;
    for (uint64_t i = 0; i < 64; ++i) {
        acc = acc + i * i;
    }
    return acc;
}

RunStats one_run(double cycles_per_ns, uint64_t samples) {
    Histogram hist;
    uint64_t anomalies = 0;
    for (uint64_t i = 0; i < samples; ++i) {
        const uint64_t start = cycle_start();
        (void)demo_workload();
        const uint64_t end = cycle_end();
        if (end < start) {
            ++anomalies;
            continue;
        }
        const auto ns = static_cast<uint64_t>(static_cast<double>(end - start) / cycles_per_ns);
        hist.record(ns);
    }
    RunStats stats;
    stats.samples = hist.count();
    stats.anomalies = anomalies;
    stats.p50_ns = hist.percentile_milli(50'000);
    stats.p99_ns = hist.percentile_milli(99'000);
    stats.p999_ns = hist.percentile_milli(99'900);
    return stats;
}

} // namespace

BenchResult run_demo_bench(uint64_t calibration_window_ns) {
    BenchResult result;
    result.name = "demo/trivial-function";
    result.machine = read_machine_config();
    result.cycle_source = cycle_source_name();
    result.clock = check_clock();
    result.calibration = calibrate(calibration_window_ns);
    const double cpns =
        result.calibration.cycles_per_ns > 0 ? result.calibration.cycles_per_ns : 1.0;

    // empty-probe overhead: rdtscp pair around a no-op
    {
        Histogram hist;
        for (int i = 0; i < 10'000; ++i) {
            const uint64_t start = cycle_start();
            const uint64_t end = cycle_end();
            if (end >= start) {
                hist.record(static_cast<uint64_t>(static_cast<double>(end - start) / cpns));
            }
        }
        result.empty_probe_p50_ns = hist.percentile_milli(50'000);
    }

    constexpr uint64_t kSamples = 20'000;
    constexpr uint64_t kWarmup = 2'000; // discarded
    (void)one_run(cpns, kWarmup);
    for (int i = 0; i < 5; ++i) {
        result.runs.push_back(one_run(cpns, kSamples));
    }
    return result;
}

} // namespace ob::bench
