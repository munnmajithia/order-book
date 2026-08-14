#pragma once

// Fixed-memory log-bucketed histogram (HdrHistogram-style), 8 precision bits.
//
//   values 0..255            stored exactly, one counter each
//   values 256..2^30-1       128 linear sub-buckets per power-of-two
//                            magnitude, bounding relative error at 1/128
//   values >= 2^30           saturate into a separate overflow counter
//
// 256 + 22*128 = 3072 counters, 24 KB, no allocation ever. Percentiles use
// the nearest-rank definition in integer arithmetic and report the highest
// value the rank's bucket can hold, so a reported number is never better
// than the truth.

#include <array>
#include <bit>
#include <cstdint>

namespace ob::bench {

class Histogram {
  public:
    static constexpr uint64_t kExactLimit = 256; // below this, exact
    static constexpr int kSubBucketBits = 7;     // 128 sub-buckets
    static constexpr uint64_t kMaxValue = (1ULL << 30) - 1;
    static constexpr std::size_t kBucketCount = 3072;

    void record(uint64_t value) {
        if (value > kMaxValue) {
            ++overflow_;
            ++count_;
            return;
        }
        ++buckets_[index_of(value)];
        ++count_;
    }

    [[nodiscard]] uint64_t count() const { return count_; }
    [[nodiscard]] uint64_t overflow() const { return overflow_; }

    // p in thousandths of a percent: p50 = 50000, p99.9 = 99900.
    [[nodiscard]] uint64_t percentile_milli(uint64_t p_milli) const {
        if (count_ == 0) {
            return 0;
        }
        // nearest rank, ceil(p/100 * count) in integers
        uint64_t rank = (count_ * p_milli + 99'999) / 100'000;
        if (rank == 0) {
            rank = 1;
        }
        uint64_t seen = 0;
        for (std::size_t i = 0; i < kBucketCount; ++i) {
            seen += buckets_[i];
            if (seen >= rank) {
                return highest_in_bucket(i);
            }
        }
        // rank falls into the overflow counter
        return kMaxValue + 1;
    }

    static constexpr std::size_t index_of(uint64_t value) {
        if (value < kExactLimit) {
            return static_cast<std::size_t>(value);
        }
        const int msb = std::bit_width(value) - 1; // 8..29
        const auto sub = static_cast<std::size_t>((value >> (msb - kSubBucketBits)) &
                                                  ((1U << kSubBucketBits) - 1));
        return kExactLimit + static_cast<std::size_t>(msb - 8) * (1U << kSubBucketBits) + sub;
    }

    static constexpr uint64_t highest_in_bucket(std::size_t index) {
        if (index < kExactLimit) {
            return index;
        }
        const std::size_t rel = index - kExactLimit;
        const int msb = static_cast<int>(rel >> kSubBucketBits) + 8;
        const uint64_t sub = rel & ((1U << kSubBucketBits) - 1);
        const int shift = msb - kSubBucketBits;
        const uint64_t base = ((1ULL << kSubBucketBits) + sub) << shift;
        return base + ((1ULL << shift) - 1);
    }

  private:
    std::array<uint64_t, kBucketCount> buckets_{};
    uint64_t overflow_ = 0;
    uint64_t count_ = 0;
};

} // namespace ob::bench
