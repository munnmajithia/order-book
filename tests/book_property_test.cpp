// Property tests over generated op sequences: a shadow model tracks every
// live order and the running share totals, and after every operation the
// book must agree with the model, pass its own invariant sweep, and at the
// end conserve shares exactly — added = executed + canceled + removed +
// still resting. The flow generator is a fixed-seed xorshift, so failures
// replay deterministically.
#include "book/reference_book.hpp"
#include "itch/messages.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

AddOrder add(uint16_t locate, uint64_t ref, char side, uint32_t shares, uint32_t price) {
    AddOrder msg{};
    msg.header.type = 'A';
    msg.header.locate = be<2>(locate);
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

OrderExecutedPrice executed_price(uint64_t ref, uint32_t shares, uint32_t price) {
    OrderExecutedPrice msg{};
    msg.executed.header.type = 'C';
    msg.executed.order_ref = be<8>(ref);
    msg.executed.executed_shares = be<4>(shares);
    msg.printable = 'Y';
    msg.execution_price = be<4>(price);
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

struct Xorshift {
    uint64_t state;

    uint64_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    // Uniform-enough draw in [0, bound) for test flow generation.
    uint64_t below(uint64_t bound) { return next() % bound; }
};

struct ShadowOrder {
    uint16_t locate;
    char side;
    uint32_t price;
    uint32_t shares;
};

// Sum of "B/A price qty count" level lines in the canonical snapshot, i.e.
// every share the book itself claims is resting.
uint64_t snapshot_resting_shares(const ReferenceBook& book) {
    std::ostringstream out;
    book.write_snapshot(out);
    std::istringstream in(out.str());
    uint64_t total = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || (line[0] != 'B' && line[0] != 'A') || line[1] != ' ') {
            continue;
        }
        std::istringstream fields(line);
        std::string tag;
        uint64_t price = 0;
        uint64_t qty = 0;
        fields >> tag >> price >> qty;
        total += qty;
    }
    return total;
}

// Bid prices stay strictly below ask prices per stock, so the generated flow
// never crosses and every sweep can demand an uncrossed book.
constexpr uint32_t kBidLow = 1000;
constexpr uint32_t kBidBand = 4000; // bids in [1000, 5000)
constexpr uint32_t kAskLow = 5000;
constexpr uint32_t kAskBand = 4000; // asks in [5000, 9000)

TEST(BookProperty, GeneratedFlowsConserveSharesAndInvariants) {
    for (const uint64_t seed :
         {0x9E3779B97F4A7C15ULL, 0xC2B2AE3D27D4EB4FULL, 0x165667B19E3779F9ULL}) {
        Xorshift rng{seed};
        ReferenceBook book;
        std::map<uint64_t, ShadowOrder> model;
        std::vector<uint64_t> live; // refs, for O(1) random victim choice
        uint64_t next_ref = 1;
        uint64_t added = 0;
        uint64_t exec = 0;
        uint64_t canceled = 0;
        uint64_t removed = 0; // shares still resting when their order left the book

        const auto pick_victim = [&] {
            const std::size_t i = rng.below(live.size());
            return std::pair{i, live[i]};
        };
        const auto forget = [&](std::size_t i, uint64_t ref) {
            live[i] = live.back();
            live.pop_back();
            model.erase(ref);
        };

        for (int op = 0; op < 20'000; ++op) {
            const uint64_t roll = rng.below(100);
            if (live.empty() || roll < 40) {
                const auto locate = static_cast<uint16_t>(1 + rng.below(4));
                const char side = rng.below(2) == 0 ? 'B' : 'S';
                const auto shares = static_cast<uint32_t>(1 + rng.below(1000));
                const uint32_t price = side == 'B'
                                           ? kBidLow + static_cast<uint32_t>(rng.below(kBidBand))
                                           : kAskLow + static_cast<uint32_t>(rng.below(kAskBand));
                const uint64_t ref = next_ref++;
                book.apply(add(locate, ref, side, shares, price));
                model.emplace(ref, ShadowOrder{locate, side, price, shares});
                live.push_back(ref);
                added += shares;
            } else if (roll < 55) {
                const auto [i, ref] = pick_victim();
                ShadowOrder& order = model.at(ref);
                const auto shares = static_cast<uint32_t>(1 + rng.below(order.shares));
                book.apply(executed(ref, shares));
                exec += shares;
                order.shares -= shares;
                if (order.shares == 0) {
                    forget(i, ref);
                }
            } else if (roll < 65) {
                const auto [i, ref] = pick_victim();
                ShadowOrder& order = model.at(ref);
                const auto shares = static_cast<uint32_t>(1 + rng.below(order.shares));
                book.apply(executed_price(ref, shares, order.price));
                exec += shares;
                order.shares -= shares;
                if (order.shares == 0) {
                    forget(i, ref);
                }
            } else if (roll < 75) {
                const auto [i, ref] = pick_victim();
                ShadowOrder& order = model.at(ref);
                const auto shares = static_cast<uint32_t>(1 + rng.below(order.shares));
                book.apply(cancel(ref, shares));
                canceled += shares;
                order.shares -= shares;
                if (order.shares == 0) {
                    forget(i, ref);
                }
            } else if (roll < 90) {
                const auto [i, ref] = pick_victim();
                book.apply(del(ref));
                removed += model.at(ref).shares;
                forget(i, ref);
            } else {
                const auto [i, ref] = pick_victim();
                const ShadowOrder order = model.at(ref);
                const auto shares = static_cast<uint32_t>(1 + rng.below(1000));
                const uint32_t price = order.side == 'B'
                                           ? kBidLow + static_cast<uint32_t>(rng.below(kBidBand))
                                           : kAskLow + static_cast<uint32_t>(rng.below(kAskBand));
                const uint64_t ref2 = next_ref++;
                book.apply(replace(ref, ref2, shares, price));
                // Replace abandons the original's remaining shares and adds
                // fresh ones, so both totals move.
                removed += order.shares;
                added += shares;
                forget(i, ref);
                model.emplace(ref2, ShadowOrder{order.locate, order.side, price, shares});
                live.push_back(ref2);
            }

            ASSERT_EQ(book.open_order_count(), model.size()) << "seed " << seed << " op " << op;
            if (op % 64 == 0) {
                ASSERT_NO_THROW(book.check_invariants(true)) << "seed " << seed << " op " << op;
            }
        }

        book.check_invariants(true);
        uint64_t model_resting = 0;
        for (const auto& [ref, order] : model) {
            model_resting += order.shares;
        }
        const uint64_t book_resting = snapshot_resting_shares(book);
        EXPECT_EQ(book_resting, model_resting) << "seed " << seed;
        EXPECT_EQ(added, exec + canceled + removed + book_resting) << "seed " << seed;

        // Teardown: deleting every live order must empty the book exactly.
        for (const uint64_t ref : live) {
            book.apply(del(ref));
        }
        book.check_invariants(true);
        EXPECT_EQ(book.open_order_count(), 0U) << "seed " << seed;
        EXPECT_EQ(snapshot_resting_shares(book), 0U) << "seed " << seed;
    }
}

TEST(BookProperty, CrossedBookOnlyRejectedWhenRequired) {
    ReferenceBook book;
    book.apply(add(1, 1, 'B', 100, 6000));
    book.apply(add(1, 2, 'S', 100, 5000)); // ask below bid: crossed
    ASSERT_NO_THROW(book.check_invariants(false));
    EXPECT_THROW(book.check_invariants(true), BookError);
    book.apply(del(2));
    ASSERT_NO_THROW(book.check_invariants(true));
}

TEST(BookProperty, LockedBookCountsAsCrossed) {
    ReferenceBook book;
    book.apply(add(1, 1, 'B', 100, 5000));
    book.apply(add(1, 2, 'S', 100, 5000));
    EXPECT_THROW(book.check_invariants(true), BookError);
}

} // namespace
