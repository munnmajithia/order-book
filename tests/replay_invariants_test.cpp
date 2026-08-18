// Invariants over a real replay: stream the whole CI fixture through the
// decoder into the reference book and run the structural invariant check
// periodically and at the end. This is the "invariants in debug replay mode"
// acceptance for the reference book, on real exchange bytes rather than
// generated flow.
//
// The check is O(open orders), so it samples every N messages instead of
// running on each one; a full check at end-of-stream covers the final state
// exactly. Crossing is recorded, not asserted: real ITCH data locks and
// crosses transiently and that is not a structural defect.

#include "book/reference_book.hpp"
#include "itch/decoder.hpp"
#include "itch/reader.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

using namespace ob::itch;
using ob::book::ReferenceBook;

constexpr uint64_t kCheckEvery = 2000;

struct CheckingHandler {
    ReferenceBook book;
    uint64_t applied = 0;
    uint64_t checks = 0;

    template <typename Msg> void operator()(const Msg& msg) {
        book.apply(msg);
        if (++applied % kCheckEvery == 0) {
            book.check_invariants();
            ++checks;
        }
    }
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): handler interface
    void on_skipped(const Frame& /*frame*/) {}
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): handler interface
    void on_unknown(const Frame& frame) { FAIL() << "unknown type in fixture at " << frame.offset; }
};

TEST(ReplayInvariants, HoldAcrossTheWholeFixture) {
    const MappedFile file(std::string(OB_TEST_DATA_DIR) + "/fixture.itch");
    FrameReader reader(file.bytes());
    CheckingHandler handler;
    while (const auto frame = reader.next()) {
        decode(*frame, handler);
    }
    handler.book.check_invariants(); // exact final state

    EXPECT_EQ(handler.applied, 151'476U);
    EXPECT_GT(handler.checks, 0U);
    // The fixture drains to a book that still holds resting orders; the golden
    // snapshot pins the exact shape, this pins that it is internally consistent.
    EXPECT_GT(handler.book.open_order_count(), 0U);
}

} // namespace
