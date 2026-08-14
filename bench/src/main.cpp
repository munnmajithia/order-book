// Demo bench binary: measures a trivial function through the full pipeline
// (qualification, calibration, histogram, JSON result). Exists to prove the
// harness end to end; its numbers are dev-only anywhere but the designated
// machine, and there is no designated machine yet.

#include "bench/clock.hpp"
#include "bench/result.hpp"

#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    bool allow_unqualified = false;
    std::string out_path = "bench-result.json";
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--allow-unqualified-clock") {
            allow_unqualified = true;
        } else if (arg == "--out" && i + 1 < args.size()) {
            out_path = args[++i];
        } else {
            std::cerr << "usage: ob_bench_demo [--allow-unqualified-clock] [--out FILE]\n";
            return 2;
        }
    }

    const auto qual = ob::bench::check_clock();
    if (!qual.qualified && !allow_unqualified) {
        std::cerr << "clock failed qualification:\n";
        for (const auto& p : qual.problems) {
            std::cerr << "  - " << p << "\n";
        }
        std::cerr << "rerun with --allow-unqualified-clock for a dev-only smoke test\n";
        return 1;
    }

    const auto result = ob::bench::run_demo_bench(600'000'000ULL);
    if (!ob::bench::write_json(result, out_path)) {
        std::cerr << "failed to write " << out_path << "\n";
        return 1;
    }
    std::cout << "wrote " << out_path << " (" << (result.publishable() ? "PUBLISHABLE" : "DEV-ONLY")
              << ")\n";
    return 0;
}
