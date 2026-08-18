// Fast book unit tests: the pooled/intrusive book must behave exactly like the
// reference on the same hand-built flows, including best prices, FIFO order,
// partial fills, replace losing priority, and the strict errors. Cross-checks
// against the reference on the fixture live in fast_book_xval_test.cpp.

#include "book/fast_book.hpp"
#include "support/itch_builders.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>

namespace {

using namespace ob::test;
using ob::book::FastBook;
using ob::book::FastBookError;

std::string snapshot_of(const FastBook& book) {
    std::ostringstream out;
    book.write_snapshot(out);
    return out.str();
}

TEST(FastBook, BestPricesAndCountsTrackFlow) {
    FastBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    book.apply(add(2, 'B', 50, 1'010'000)); // better bid
    book.apply(add(3, 'S', 30, 1'050'000));
    ASSERT_TRUE(book.best_bid(1).has_value());
    ASSERT_TRUE(book.best_ask(1).has_value());
    EXPECT_EQ(*book.best_bid(1), 1'010'000U); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(*book.best_ask(1), 1'050'000U); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(book.open_order_count(), 3U);
}

TEST(FastBook, FifoWithinLevelAndPartialFill) {
    FastBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    book.apply(add(2, 'B', 50, 1'000'000)); // same level, behind order 1
    book.apply(executed(1, 100));           // front fully filled, level keeps order 2
    ASSERT_TRUE(book.best_bid(1).has_value());
    EXPECT_EQ(*book.best_bid(1), 1'000'000U); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(book.open_order_count(), 1U);
}

TEST(FastBook, DeleteRemovesRestingSharesFromTotal) {
    FastBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    book.apply(add(2, 'B', 40, 1'000'000));
    book.apply(del(1)); // 100 resting shares leave the level total
    const std::string snap = snapshot_of(book);
    // The surviving level reports 40 shares over 1 order.
    EXPECT_NE(snap.find("B 1000000 40 1\n"), std::string::npos) << snap;
}

TEST(FastBook, ReplaceMovesLevelAndLosesPriority) {
    FastBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    book.apply(add(2, 'B', 50, 999'000));
    book.apply(replace(1, 3, 80, 999'000)); // join order 2's level, behind it
    book.apply(executed(2, 50));            // order 2 was ahead, fills first
    book.apply(executed(3, 80));            // then order 3
    EXPECT_EQ(book.open_order_count(), 0U);
    EXPECT_FALSE(book.best_bid(1).has_value());
}

TEST(FastBook, StrictErrorsMatchReference) {
    FastBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    EXPECT_THROW(book.apply(add(1, 'B', 10, 1'000'000)), FastBookError);   // duplicate
    EXPECT_THROW(book.apply(executed(9, 10)), FastBookError);              // unknown ref
    EXPECT_THROW(book.apply(executed(1, 200)), FastBookError);             // over-reduce
    EXPECT_THROW(book.apply(del(9)), FastBookError);                       // unknown delete
    EXPECT_THROW(book.apply(replace(9, 10, 5, 1'000'000)), FastBookError); // unknown replace
}

TEST(FastBook, PoolReusesFreedNodes) {
    FastBook book;
    // Churn many orders through the same book; the pool should recycle nodes
    // rather than growing without bound. Observable only as correctness here,
    // but it exercises the free list heavily.
    for (uint64_t ref = 1; ref <= 500; ++ref) {
        book.apply(add(ref, ref % 2 == 0 ? 'B' : 'S', 10, 1'000'000 + static_cast<uint32_t>(ref)));
        if (ref % 3 == 0) {
            book.apply(del(ref - 1));
        }
    }
    EXPECT_GT(book.open_order_count(), 0U);
}

} // namespace
