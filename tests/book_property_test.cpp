// Property tests over generated order flow. Random but valid sequences of
// add/execute/cancel/delete/replace are applied to the reference book while a
// shadow model tracks what should be live. After every message the structural
// invariants must hold and the book must stay uncrossed; at the end, shares
// must be conserved: everything added is either executed, cancelled, deleted,
// or still resting.
//
// Bids are drawn from a price band strictly below the ask band, so a valid
// flow never crosses the book by construction and is_crossed() staying false
// is a real check on the best-price accessors rather than a tautology.

#include "book/reference_book.hpp"
#include "support/itch_builders.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <random>

namespace {

using namespace ob::test;
using ob::book::ReferenceBook;

// Prices in cents-with-4-decimals; the two bands never overlap.
constexpr uint32_t kBidLow = 1'000'000;
constexpr uint32_t kBidHigh = 1'499'000;
constexpr uint32_t kAskLow = 1'500'000;
constexpr uint32_t kAskHigh = 2'000'000;

struct Live {
    char side;
    uint32_t shares;
};

// Runs one generated flow of `steps` messages and checks every property.
void run_flow(uint32_t seed, int steps) {
    std::mt19937 rng(seed);
    ReferenceBook book;
    std::map<uint64_t, Live> live; // shadow: ref -> resting side/shares
    uint64_t next_ref = 1;

    uint64_t total_added = 0;
    uint64_t total_executed = 0;
    uint64_t total_cancelled = 0;
    uint64_t total_deleted = 0; // resting shares removed by delete or replace's delete half

    const auto pick_live = [&]() -> uint64_t {
        // Uniformly pick an existing ref, or 0 when the book is empty.
        if (live.empty()) {
            return 0;
        }
        std::uniform_int_distribution<std::size_t> idx(0, live.size() - 1);
        auto it = live.begin();
        std::advance(it, static_cast<long>(idx(rng)));
        return it->first;
    };

    const auto do_add = [&]() {
        std::bernoulli_distribution is_bid(0.5);
        const bool bid = is_bid(rng);
        const char side = bid ? 'B' : 'S';
        std::uniform_int_distribution<uint32_t> price(bid ? kBidLow : kAskLow,
                                                      bid ? kBidHigh : kAskHigh);
        std::uniform_int_distribution<uint32_t> shares(1, 1000);
        const uint64_t ref = next_ref++;
        const uint32_t sh = shares(rng);
        book.apply(add(ref, side, sh, price(rng)));
        live[ref] = Live{.side = side, .shares = sh};
        total_added += sh;
    };

    for (int i = 0; i < steps; ++i) {
        std::uniform_int_distribution<int> op(0, 9);
        const int roll = op(rng);
        if (roll < 4 || live.empty()) { // ~40% adds, and always add when empty
            do_add();
        } else if (roll < 6) { // execute
            const uint64_t ref = pick_live();
            auto& l = live.at(ref);
            std::uniform_int_distribution<uint32_t> amt(1, l.shares);
            const uint32_t sh = amt(rng);
            book.apply(executed(ref, sh));
            total_executed += sh;
            l.shares -= sh;
            if (l.shares == 0) {
                live.erase(ref);
            }
        } else if (roll < 8) { // cancel
            const uint64_t ref = pick_live();
            auto& l = live.at(ref);
            std::uniform_int_distribution<uint32_t> amt(1, l.shares);
            const uint32_t sh = amt(rng);
            book.apply(cancel(ref, sh));
            total_cancelled += sh;
            l.shares -= sh;
            if (l.shares == 0) {
                live.erase(ref);
            }
        } else if (roll < 9) { // delete
            const uint64_t ref = pick_live();
            total_deleted += live.at(ref).shares;
            book.apply(del(ref));
            live.erase(ref);
        } else { // replace: delete the old resting order, add a new one same side
            const uint64_t ref = pick_live();
            const Live old = live.at(ref);
            const bool bid = old.side == 'B';
            std::uniform_int_distribution<uint32_t> price(bid ? kBidLow : kAskLow,
                                                          bid ? kBidHigh : kAskHigh);
            std::uniform_int_distribution<uint32_t> shares(1, 1000);
            const uint64_t new_ref = next_ref++;
            const uint32_t sh = shares(rng);
            book.apply(replace(ref, new_ref, sh, price(rng)));
            total_deleted += old.shares; // old resting shares leave the book
            total_added += sh;           // new order's shares enter it
            live.erase(ref);
            live[new_ref] = Live{.side = old.side, .shares = sh};
        }

        book.check_invariants();
        ASSERT_FALSE(book.is_crossed()) << "seed " << seed << " step " << i;
        ASSERT_EQ(book.open_order_count(), live.size()) << "seed " << seed << " step " << i;
    }

    // Shares are conserved across the whole flow.
    uint64_t resting = 0;
    for (const auto& [ref, l] : live) {
        (void)ref;
        resting += l.shares;
    }
    EXPECT_EQ(total_added, total_executed + total_cancelled + total_deleted + resting)
        << "seed " << seed;

    // The book agrees with the shadow on resting shares. Every generated order
    // uses the default locate 1, so the two sides of that stock hold it all.
    const uint64_t book_resting = book.side_quantity(1, 'B') + book.side_quantity(1, 'S');
    EXPECT_EQ(book_resting, resting) << "seed " << seed;
}

TEST(BookProperty, ConservationAndInvariantsAcrossManyFlows) {
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        run_flow(seed, 1500);
    }
}

TEST(BookProperty, ShortFlowsFromManySeeds) {
    // Many short flows reach the empty-book and single-order edges often.
    for (uint32_t seed = 1000; seed < 1200; ++seed) {
        run_flow(seed, 20);
    }
}

} // namespace
