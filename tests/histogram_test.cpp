// Histogram correctness against exact reference percentiles computed from
// the raw samples, per the methodology's error bound of 1/128.
#include "bench/histogram.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

using ob::bench::Histogram;

// Exact nearest-rank percentile on raw samples, same definition the
// histogram uses: rank = ceil(p/100 * n) in integer arithmetic.
uint64_t exact_percentile(std::vector<uint64_t> samples, uint64_t p_milli) {
    std::sort(samples.begin(), samples.end());
    uint64_t rank = (samples.size() * p_milli + 99'999) / 100'000;
    if (rank == 0) {
        rank = 1;
    }
    return samples[rank - 1];
}

void expect_within_bound(uint64_t reported, uint64_t exact) {
    // The reported value is the highest value in the exact value's bucket:
    // never below the truth, and above by at most the bucket width (1/128
    // relative for log buckets, 0 for exact buckets).
    EXPECT_GE(reported, exact);
    const double bound =
        exact < Histogram::kExactLimit ? 0.0 : static_cast<double>(exact) / 128.0 + 1.0;
    EXPECT_LE(static_cast<double>(reported) - static_cast<double>(exact), bound);
}

TEST(Histogram, ExactValuesBelow256) {
    Histogram h;
    for (uint64_t v = 0; v < 256; ++v) {
        h.record(v);
    }
    EXPECT_EQ(h.count(), 256U);
    // every value is its own bucket down here
    EXPECT_EQ(h.percentile_milli(50'000), 127U);
    EXPECT_EQ(h.percentile_milli(100'000), 255U);
}

TEST(Histogram, BucketIndexRoundTrips) {
    // The highest value of every bucket must map back to that bucket.
    for (std::size_t i = 0; i < Histogram::kBucketCount; ++i) {
        EXPECT_EQ(Histogram::index_of(Histogram::highest_in_bucket(i)), i) << "bucket " << i;
    }
    EXPECT_EQ(Histogram::highest_in_bucket(Histogram::kBucketCount - 1), Histogram::kMaxValue);
}

TEST(Histogram, PercentilesMatchExactWithinBound) {
    std::mt19937_64 rng(42);
    std::lognormal_distribution<double> dist(5.0, 1.5);
    std::vector<uint64_t> samples;
    samples.reserve(100'000);
    Histogram h;
    for (int i = 0; i < 100'000; ++i) {
        const auto v = static_cast<uint64_t>(dist(rng));
        samples.push_back(v);
        h.record(v);
    }
    for (const uint64_t p : {1'000ULL, 25'000ULL, 50'000ULL, 90'000ULL, 99'000ULL, 99'900ULL}) {
        expect_within_bound(h.percentile_milli(p), exact_percentile(samples, p));
    }
}

TEST(Histogram, OverflowSaturatesAndIsCounted) {
    Histogram h;
    h.record(Histogram::kMaxValue);
    h.record(Histogram::kMaxValue + 1);
    h.record(~0ULL);
    EXPECT_EQ(h.count(), 3U);
    EXPECT_EQ(h.overflow(), 2U);
    EXPECT_EQ(h.percentile_milli(1'000), Histogram::kMaxValue);
    // ranks landing in the overflow counter report saturation, not a lie
    EXPECT_EQ(h.percentile_milli(100'000), Histogram::kMaxValue + 1);
}

} // namespace
