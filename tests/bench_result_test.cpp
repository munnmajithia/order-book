// Clock calibration sanity and the JSON result pipeline, smoke-test sized.
// Everything here is dev-only by construction; the tests assert exactly that.
#include "bench/clock.hpp"
#include "bench/result.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

TEST(Clock, MonotonicAcrossProbe) {
    const uint64_t a = ob::bench::cycle_start();
    const uint64_t b = ob::bench::cycle_end();
    EXPECT_GE(b, a);
}

TEST(Clock, CalibrationProducesPlausibleFrequency) {
    // 50 ms window: smoke-test sized, therefore dev-only, but the factor
    // must land in a physically plausible band (0.1 - 10 GHz).
    const auto cal = ob::bench::calibrate(50'000'000ULL);
    EXPECT_GE(cal.window_ns, 50'000'000ULL);
    EXPECT_GT(cal.cycles_per_ns, 0.1);
    EXPECT_LT(cal.cycles_per_ns, 10.0);
}

TEST(BenchResult, DemoBenchIsStampedDevOnlyHere) {
    const auto result = ob::bench::run_demo_bench(20'000'000ULL);
    EXPECT_FALSE(result.publishable());
    const auto reasons = result.disqualifiers();
    // no designated machine exists, so that reason must always be present
    EXPECT_NE(std::find(reasons.begin(), reasons.end(), "no designated bench machine"),
              reasons.end());
    // the smoke-sized calibration window is itself disqualifying
    EXPECT_NE(std::find(reasons.begin(), reasons.end(), "calibration window under 500 ms"),
              reasons.end());
    EXPECT_EQ(result.runs.size(), 5U);
    for (const auto& run : result.runs) {
        EXPECT_GT(run.samples, 0U);
    }
}

TEST(BenchResult, WritesWellFormedJson) {
    const auto result = ob::bench::run_demo_bench(20'000'000ULL);
    const std::string path = testing::TempDir() + "/bench-result.json";
    ASSERT_TRUE(ob::bench::write_json(result, path));

    const std::ifstream in(path);
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string json = buf.str();
    EXPECT_NE(json.find("\"stamp\": \"DEV-ONLY\""), std::string::npos);
    EXPECT_NE(json.find("\"cpu_model\""), std::string::npos);
    EXPECT_NE(json.find("\"cycles_per_ns\""), std::string::npos);
    EXPECT_NE(json.find("\"p999_ns\""), std::string::npos);
    std::remove(path.c_str());
}

} // namespace
