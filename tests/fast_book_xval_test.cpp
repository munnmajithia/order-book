// Cross-validation: replay the whole CI fixture through the reference book and
// the fast book together, comparing their snapshots every N messages and once
// exactly at the end. Any divergence is a stop-the-world bug in the fast book,
// which is only trustworthy because the reference is obviously correct.
//
// This is the fixture half of C3.1's cross-validation. The full-day replay
// (sampled) is a local/dev check that needs the multi-gigabyte day file and is
// not run in CI.

#include "book/fast_book.hpp"
#include "book/reference_book.hpp"
#include "itch/decoder.hpp"
#include "itch/reader.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>

namespace {

using namespace ob::itch;
using ob::book::FastBook;
using ob::book::ReferenceBook;

constexpr uint64_t kCompareEvery = 1000;

std::string snapshot(const ReferenceBook& book) {
    std::ostringstream out;
    book.write_snapshot(out);
    return out.str();
}
std::string snapshot(const FastBook& book) {
    std::ostringstream out;
    book.write_snapshot(out);
    return out.str();
}

struct DualHandler {
    ReferenceBook ref;
    FastBook fast;
    uint64_t applied = 0;
    uint64_t comparisons = 0;
    bool diverged = false;
    uint64_t diverged_at = 0;

    template <typename Msg> void operator()(const Msg& msg) {
        ref.apply(msg);
        fast.apply(msg);
        if (++applied % kCompareEvery == 0) {
            ++comparisons;
            if (!diverged && snapshot(ref) != snapshot(fast)) {
                diverged = true;
                diverged_at = applied;
            }
        }
    }
    void on_skipped(const Frame& /*frame*/) {}
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): handler interface
    void on_unknown(const Frame& frame) { FAIL() << "unknown type in fixture at " << frame.offset; }
};

TEST(FastBookCrossValidation, MatchesReferenceAcrossTheFixture) {
    const MappedFile file(std::string(OB_TEST_DATA_DIR) + "/fixture.itch");
    FrameReader reader(file.bytes());
    DualHandler handler;
    while (const auto frame = reader.next()) {
        decode(*frame, handler);
    }

    EXPECT_FALSE(handler.diverged) << "books diverged at message " << handler.diverged_at;
    EXPECT_EQ(handler.applied, 151'476U);
    EXPECT_GT(handler.comparisons, 0U);
    // Full comparison at end of fixture: byte-for-byte identical state.
    EXPECT_EQ(snapshot(handler.ref), snapshot(handler.fast));
    EXPECT_EQ(handler.ref.open_order_count(), handler.fast.open_order_count());
    EXPECT_GT(handler.fast.open_order_count(), 0U);
}

} // namespace
