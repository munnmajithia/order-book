// Cross-validation: the fast book must be observably identical to the
// reference book. Replays the CI fixture through both with sampled snapshot
// comparison along the way and a full comparison (against each other and the
// committed golden file) at the end, then repeats the property suite's
// generated flows with the pair in lockstep. Any divergence is a
// stop-the-world bug in the fast book.
//
// Also unit-covers the fast book's building blocks: the slab pool recycles
// nodes and the open-addressing map survives churn over tombstones.
#include "book/fast_book.hpp"
#include "book/open_map.hpp"
#include "book/reference_book.hpp"
#include "book/slab_pool.hpp"
#include "itch/decoder.hpp"
#include "itch/messages.hpp"
#include "itch/reader.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ob::itch;
using ob::book::FastBook;
using ob::book::OpenMap;
using ob::book::ReferenceBook;
using ob::book::SlabPool;

template <typename Book> std::string snapshot_of(const Book& book) {
    std::ostringstream out;
    book.write_snapshot(out);
    return out.str();
}

struct PairHandler {
    ReferenceBook reference;
    FastBook fast;

    template <typename Msg> void operator()(const Msg& msg) {
        reference.apply(msg);
        fast.apply(msg);
    }
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): handler interface
    void on_skipped(const Frame& frame) { (void)frame; }
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): handler interface
    void on_unknown(const Frame& frame) {
        FAIL() << "unknown type in fixture at offset " << frame.offset;
    }
};

TEST(FastBookCross, FixtureReplayMatchesReferenceAndGolden) {
    const MappedFile file(std::string(OB_TEST_DATA_DIR) + "/fixture.itch");
    FrameReader reader(file.bytes());
    PairHandler handler;
    uint64_t frames = 0;
    while (const auto frame = reader.next()) {
        decode(*frame, handler);
        // Sampled comparison during replay, full comparison at the end.
        if (++frames % 16384 == 0) {
            ASSERT_EQ(handler.fast.open_order_count(), handler.reference.open_order_count())
                << "diverged by frame " << frames;
            ASSERT_EQ(snapshot_of(handler.fast), snapshot_of(handler.reference))
                << "diverged by frame " << frames;
        }
    }

    const std::string fast_snapshot = snapshot_of(handler.fast);
    EXPECT_EQ(fast_snapshot, snapshot_of(handler.reference));

    const std::ifstream in(std::string(OB_TEST_DATA_DIR) + "/fixture-snapshot.txt");
    ASSERT_TRUE(in);
    std::stringstream committed;
    committed << in.rdbuf();
    EXPECT_EQ(fast_snapshot, committed.str());
}

template <std::size_t N> BeInt<N> be(uint64_t v) {
    BeInt<N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out.bytes[N - 1 - i] = static_cast<unsigned char>(v & 0xFFU);
        v >>= 8U;
    }
    return out;
}

struct Xorshift {
    uint64_t state;

    uint64_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

    uint64_t below(uint64_t bound) { return next() % bound; }
};

// Lockstep generated flows: same op stream into both books, snapshots
// compared every stretch. Mirrors the property suite's generator, but the
// oracle here is the reference book itself.
TEST(FastBookCross, GeneratedFlowsStayInLockstep) {
    for (const uint64_t seed : {0xA076'1D64'78BD'642FULL, 0xE703'7ED1'A0B4'28DBULL}) {
        Xorshift rng{seed};
        ReferenceBook reference;
        FastBook fast;
        std::map<uint64_t, uint32_t> live_shares; // ref -> remaining
        std::vector<uint64_t> live;
        uint64_t next_ref = 1;

        const auto both = [&](const auto& msg) {
            reference.apply(msg);
            fast.apply(msg);
        };

        for (int op = 0; op < 10'000; ++op) {
            const uint64_t roll = rng.below(100);
            if (live.empty() || roll < 45) {
                AddOrder msg{};
                msg.header.type = 'A';
                msg.header.locate = be<2>(1 + rng.below(3));
                msg.order_ref = be<8>(next_ref);
                msg.side = rng.below(2) == 0 ? 'B' : 'S';
                msg.shares = be<4>(1 + rng.below(500));
                msg.stock = Alpha<8>{{'T', 'E', 'S', 'T', ' ', ' ', ' ', ' '}};
                msg.price = be<4>(1000 + rng.below(8000));
                both(msg);
                live_shares[next_ref] = static_cast<uint32_t>(msg.shares.value());
                live.push_back(next_ref);
                ++next_ref;
            } else if (roll < 60) {
                const std::size_t i = rng.below(live.size());
                const uint64_t ref = live[i];
                const auto shares = static_cast<uint32_t>(1 + rng.below(live_shares.at(ref)));
                OrderExecuted msg{};
                msg.header.type = 'E';
                msg.order_ref = be<8>(ref);
                msg.executed_shares = be<4>(shares);
                both(msg);
                live_shares.at(ref) -= shares;
                if (live_shares.at(ref) == 0) {
                    live[i] = live.back();
                    live.pop_back();
                    live_shares.erase(ref);
                }
            } else if (roll < 75) {
                const std::size_t i = rng.below(live.size());
                const uint64_t ref = live[i];
                const auto shares = static_cast<uint32_t>(1 + rng.below(live_shares.at(ref)));
                OrderCancel msg{};
                msg.header.type = 'X';
                msg.order_ref = be<8>(ref);
                msg.canceled_shares = be<4>(shares);
                both(msg);
                live_shares.at(ref) -= shares;
                if (live_shares.at(ref) == 0) {
                    live[i] = live.back();
                    live.pop_back();
                    live_shares.erase(ref);
                }
            } else if (roll < 90) {
                const std::size_t i = rng.below(live.size());
                const uint64_t ref = live[i];
                OrderDelete msg{};
                msg.header.type = 'D';
                msg.order_ref = be<8>(ref);
                both(msg);
                live[i] = live.back();
                live.pop_back();
                live_shares.erase(ref);
            } else {
                const std::size_t i = rng.below(live.size());
                const uint64_t ref = live[i];
                OrderReplace msg{};
                msg.header.type = 'U';
                msg.original_ref = be<8>(ref);
                msg.new_ref = be<8>(next_ref);
                msg.shares = be<4>(1 + rng.below(500));
                msg.price = be<4>(1000 + rng.below(8000));
                both(msg);
                live_shares.erase(ref);
                live[i] = next_ref;
                live_shares[next_ref] = static_cast<uint32_t>(msg.shares.value());
                ++next_ref;
            }

            ASSERT_EQ(fast.open_order_count(), reference.open_order_count())
                << "seed " << seed << " op " << op;
            if (op % 512 == 0) {
                ASSERT_EQ(snapshot_of(fast), snapshot_of(reference))
                    << "seed " << seed << " op " << op;
            }
        }
        EXPECT_EQ(snapshot_of(fast), snapshot_of(reference)) << "seed " << seed;
    }
}

TEST(SlabPoolUnit, RecyclesNodes) {
    struct Node {
        Node* next;
        uint64_t payload;
    };
    SlabPool<Node> pool;
    Node* a = pool.allocate();
    Node* b = pool.allocate();
    EXPECT_NE(a, b);
    pool.release(a);
    Node* c = pool.allocate(); // free list hands back the released node
    EXPECT_EQ(c, a);
    pool.release(b);
    pool.release(c);
}

TEST(OpenMapUnit, SurvivesChurnAcrossTombstones) {
    OpenMap<uint64_t> map(8); // tiny reserve to force rehash under churn
    std::vector<uint64_t> values(4096);
    for (uint64_t round = 0; round < 4; ++round) {
        for (uint64_t k = 1; k <= 4096; ++k) {
            values[k - 1] = round * 100'000 + k;
            map.insert(k, &values[k - 1]);
        }
        EXPECT_EQ(map.size(), 4096U);
        for (uint64_t k = 1; k <= 4096; ++k) {
            ASSERT_NE(map.find(k), nullptr);
            EXPECT_EQ(*map.find(k), round * 100'000 + k);
        }
        for (uint64_t k = 1; k <= 4096; ++k) {
            map.erase(k);
        }
        EXPECT_EQ(map.size(), 0U);
        EXPECT_EQ(map.find(1), nullptr);
    }
    EXPECT_THROW(
        {
            map.insert(7, values.data());
            map.insert(7, &values[1]);
        },
        std::logic_error);
}

} // namespace
