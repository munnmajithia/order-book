// Unit tests for the reference book's invariant checks and best-price
// accessors on hand-built flows. The structural invariants must hold after
// every message the public API accepts; crossing is detected but not an error.

#include "book/reference_book.hpp"
#include "support/itch_builders.hpp"

#include <gtest/gtest.h>

namespace {

using namespace ob::test;
using ob::book::ReferenceBook;

TEST(BookInvariants, BestPricesTrackBothSides) {
    ReferenceBook book;
    EXPECT_FALSE(book.best_bid(1).has_value());
    EXPECT_FALSE(book.best_ask(1).has_value());

    book.apply(add(1, 'B', 100, 1'000'000));
    book.apply(add(2, 'B', 100, 1'010'000)); // higher bid becomes best
    book.apply(add(3, 'S', 100, 1'050'000));
    book.apply(add(4, 'S', 100, 1'040'000)); // lower ask becomes best

    const auto bid = book.best_bid(1);
    const auto ask = book.best_ask(1);
    ASSERT_TRUE(bid.has_value());
    ASSERT_TRUE(ask.has_value());
    EXPECT_EQ(*bid, 1'010'000U); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(*ask, 1'040'000U); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_FALSE(book.is_crossed());
    book.check_invariants();
}

TEST(BookInvariants, BestBidFallsBackWhenTopLevelClears) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'010'000));
    book.apply(add(2, 'B', 100, 1'000'000));
    book.apply(del(1)); // best bid gone, falls to the next level
    const auto bid = book.best_bid(1);
    ASSERT_TRUE(bid.has_value());
    EXPECT_EQ(*bid, 1'000'000U); // NOLINT(bugprone-unchecked-optional-access)
    book.check_invariants();
}

TEST(BookInvariants, SideQuantitySumsRestingShares) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    book.apply(add(2, 'B', 40, 1'000'000)); // same level
    book.apply(add(3, 'B', 25, 999'000));   // deeper level
    book.apply(add(4, 'S', 10, 1'100'000));
    EXPECT_EQ(book.side_quantity(1, 'B'), 165U);
    EXPECT_EQ(book.side_quantity(1, 'S'), 10U);
    book.apply(executed(1, 30)); // 100 -> 70
    EXPECT_EQ(book.side_quantity(1, 'B'), 135U);
    book.check_invariants();
}

TEST(BookInvariants, CrossingIsDetectedNotThrown) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'050'000));
    book.apply(add(2, 'S', 100, 1'050'000)); // locked: bid == ask
    EXPECT_TRUE(book.is_crossed());
    book.check_invariants(); // structural invariants still hold on a locked book

    book.apply(add(3, 'S', 50, 1'040'000)); // crossed: ask below bid
    EXPECT_TRUE(book.is_crossed());
    book.check_invariants();
}

TEST(BookInvariants, SeparateStocksDoNotCrossEachOther) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'050'000, 1)); // stock 1 bid
    book.apply(add(2, 'S', 100, 1'040'000, 2)); // stock 2 ask, lower price
    // A high bid in one stock and a low ask in another is not a cross.
    EXPECT_FALSE(book.is_crossed());
    book.check_invariants();
}

TEST(BookInvariants, HoldAfterEveryStepOfAMixedFlow) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    book.check_invariants();
    book.apply(add(2, 'S', 80, 1'100'000));
    book.check_invariants();
    book.apply(cancel(1, 30));
    book.check_invariants();
    book.apply(replace(2, 3, 60, 1'090'000));
    book.check_invariants();
    book.apply(executed(3, 60)); // fully executed, level clears
    book.check_invariants();
    book.apply(del(1));
    book.check_invariants();
    EXPECT_EQ(book.open_order_count(), 0U);
    EXPECT_FALSE(book.best_bid(1).has_value());
    EXPECT_FALSE(book.best_ask(1).has_value());
}

} // namespace
