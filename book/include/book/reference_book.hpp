#pragma once

// The reference book: the oracle everything faster is judged against.
// Written for obvious correctness and never optimized. Per-side std::map
// price levels, std::list FIFO queue per level, order-id index into the
// queues. Level totals are computed by walking the queue when asked, because
// a number derived on demand cannot drift from the orders it summarizes.

#include "itch/messages.hpp"

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ob::book {

class BookError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class ReferenceBook {
  public:
    // Decoded-message application. S and P do not change the book but flow
    // through so a replay can hand every decoded message to one place.
    void apply(const itch::SystemEvent& msg);
    void apply(const itch::StockDirectory& msg);
    void apply(const itch::AddOrder& msg);
    void apply(const itch::AddOrderMpid& msg);
    void apply(const itch::OrderExecuted& msg);
    void apply(const itch::OrderExecutedPrice& msg);
    void apply(const itch::OrderCancel& msg);
    void apply(const itch::OrderDelete& msg);
    void apply(const itch::OrderReplace& msg);
    void apply(const itch::Trade& msg);

    [[nodiscard]] std::size_t open_order_count() const { return orders_.size(); }

    // Best resting prices for a stock, or nullopt when that side has no level.
    // Bids are kept high-to-low and asks low-to-high, so each side's best is
    // the front of its map.
    [[nodiscard]] std::optional<uint32_t> best_bid(uint16_t locate) const;
    [[nodiscard]] std::optional<uint32_t> best_ask(uint16_t locate) const;

    // Total resting shares across a whole side of one stock, walked from the
    // queues so the number cannot drift from the orders it sums.
    [[nodiscard]] uint64_t side_quantity(uint16_t locate, char side) const;

    // Structural invariants that must hold after every applied message:
    //   - the order index and the queues agree (same count, and each indexed
    //     order rests where the index says, with the ref it was filed under);
    //   - no price level is present but empty;
    //   - every resting order has positive size.
    // Throws BookError naming the first violation. The book being crossed
    // (best bid >= best ask) is a separate, weaker property checked by
    // is_crossed rather than here, because real ITCH streams lock and cross
    // transiently and the structural checks must hold on that same data.
    void check_invariants() const;

    // True when some stock has both sides and its best bid is at or above its
    // best ask. Generated test flows keep the book uncrossed and assert this
    // stays false; the fixture replay only records where it happens.
    [[nodiscard]] bool is_crossed() const;

    // Canonical text snapshot of the whole book: every stock with a level,
    // every level with its total quantity and order count, prices as raw
    // fixed-point integers (4 implied decimals). Ends with an FNV-1a hash of
    // everything above it so the golden file carries its own fingerprint.
    void write_snapshot(std::ostream& out) const;

  private:
    struct OrderEntry {
        uint64_t ref;
        uint32_t shares;
    };
    using Queue = std::list<OrderEntry>;

    struct Side {
        // key: price. Bids iterate high->low, asks low->high.
        std::map<uint32_t, Queue, std::greater<>> bids;
        std::map<uint32_t, Queue, std::less<>> asks;
    };

    struct OrderLocation {
        uint16_t locate;
        char side; // 'B' or 'S'
        uint32_t price;
        Queue::iterator position;
    };

    // Checks one price level for check_invariants and records each resting ref
    // into `refs`, throwing on an empty level, a zero-share order, or a ref
    // that rests in more than one place.
    static void scan_level(const Queue& queue, char tag, uint32_t price, uint16_t locate,
                           std::unordered_set<uint64_t>& refs);

    void add_order(uint16_t locate, uint64_t ref, char side, uint32_t shares, uint32_t price);
    void reduce_order(uint64_t ref, uint32_t shares, const char* what);
    void remove_order(uint64_t ref, const char* what);
    OrderLocation& find_order(uint64_t ref, const char* what);
    Queue& queue_at(const OrderLocation& loc);

    std::unordered_map<uint16_t, Side> stocks_;
    std::unordered_map<uint16_t, std::string> names_; // locate -> trimmed symbol
    std::unordered_map<uint64_t, OrderLocation> orders_;
};

} // namespace ob::book
