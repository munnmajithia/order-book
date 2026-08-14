// Reference book semantics on hand-built messages: FIFO within a level,
// partial and full executions, cancels, deletes, replace losing time
// priority, and the strict errors that make the oracle trustworthy.
#include "book/reference_book.hpp"
#include "itch/messages.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

namespace {

using namespace ob::itch;
using ob::book::BookError;
using ob::book::ReferenceBook;

template <std::size_t N> BeInt<N> be(uint64_t v) {
    BeInt<N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out.bytes[N - 1 - i] = static_cast<unsigned char>(v & 0xFFU);
        v >>= 8U;
    }
    return out;
}

AddOrder add(uint64_t ref, char side, uint32_t shares, uint32_t price) {
    AddOrder msg{};
    msg.header.type = 'A';
    msg.header.locate = be<2>(1);
    msg.order_ref = be<8>(ref);
    msg.side = side;
    msg.shares = be<4>(shares);
    msg.stock = Alpha<8>{{'T', 'E', 'S', 'T', ' ', ' ', ' ', ' '}};
    msg.price = be<4>(price);
    return msg;
}

OrderExecuted executed(uint64_t ref, uint32_t shares) {
    OrderExecuted msg{};
    msg.header.type = 'E';
    msg.order_ref = be<8>(ref);
    msg.executed_shares = be<4>(shares);
    return msg;
}

OrderCancel cancel(uint64_t ref, uint32_t shares) {
    OrderCancel msg{};
    msg.header.type = 'X';
    msg.order_ref = be<8>(ref);
    msg.canceled_shares = be<4>(shares);
    return msg;
}

OrderDelete del(uint64_t ref) {
    OrderDelete msg{};
    msg.header.type = 'D';
    msg.order_ref = be<8>(ref);
    return msg;
}

OrderReplace replace(uint64_t orig, uint64_t next, uint32_t shares, uint32_t price) {
    OrderReplace msg{};
    msg.header.type = 'U';
    msg.original_ref = be<8>(orig);
    msg.new_ref = be<8>(next);
    msg.shares = be<4>(shares);
    msg.price = be<4>(price);
    return msg;
}

std::string snapshot_of(const ReferenceBook& book) {
    std::ostringstream out;
    book.write_snapshot(out);
    return out.str();
}

TEST(ReferenceBook, AddThenFullExecutionEmptiesBook) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'500'000));
    EXPECT_EQ(book.open_order_count(), 1U);
    book.apply(executed(1, 100));
    EXPECT_EQ(book.open_order_count(), 0U);
}

TEST(ReferenceBook, PartialExecutionLeavesRemainder) {
    ReferenceBook book;
    book.apply(add(1, 'S', 100, 1'500'000));
    book.apply(executed(1, 40));
    EXPECT_EQ(book.open_order_count(), 1U);
    EXPECT_NE(snapshot_of(book).find("A 1500000 60 1"), std::string::npos);
}

TEST(ReferenceBook, PartialCancelThenDelete) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    book.apply(cancel(1, 30));
    EXPECT_NE(snapshot_of(book).find("B 1000000 70 1"), std::string::npos);
    book.apply(del(1));
    EXPECT_EQ(book.open_order_count(), 0U);
}

TEST(ReferenceBook, LevelsAggregateAndKeepFifoOrder) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    book.apply(add(2, 'B', 50, 1'000'000));
    EXPECT_NE(snapshot_of(book).find("B 1000000 150 2"), std::string::npos);
    // Full execution of the elder order leaves only the younger.
    book.apply(executed(1, 100));
    EXPECT_NE(snapshot_of(book).find("B 1000000 50 1"), std::string::npos);
}

TEST(ReferenceBook, ReplaceMovesToNewPriceAndLosesPriority) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    book.apply(add(2, 'B', 50, 999'000));
    book.apply(replace(1, 3, 80, 999'000)); // join order 2's level, behind it
    const std::string snap = snapshot_of(book);
    EXPECT_NE(snap.find("B 999000 130 2"), std::string::npos);
    EXPECT_EQ(snap.find("B 1000000"), std::string::npos) << "old level should be gone";
    // Order 2 keeps priority: executing it first must succeed.
    book.apply(executed(2, 50));
    book.apply(executed(3, 80));
    EXPECT_EQ(book.open_order_count(), 0U);
}

TEST(ReferenceBook, StrictErrors) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    EXPECT_THROW(book.apply(add(1, 'B', 10, 1'000'000)), BookError);   // duplicate ref
    EXPECT_THROW(book.apply(executed(9, 10)), BookError);              // unknown ref
    EXPECT_THROW(book.apply(executed(1, 200)), BookError);             // over-reduce
    EXPECT_THROW(book.apply(del(9)), BookError);                       // unknown delete
    EXPECT_THROW(book.apply(replace(9, 10, 5, 1'000'000)), BookError); // unknown replace
}

TEST(ReferenceBook, SnapshotIsDeterministic) {
    ReferenceBook book;
    book.apply(add(1, 'B', 100, 1'000'000));
    book.apply(add(2, 'S', 50, 1'100'000));
    const std::string first = snapshot_of(book);
    EXPECT_EQ(first, snapshot_of(book));
    EXPECT_NE(first.find("fnv1a "), std::string::npos);
}

} // namespace
