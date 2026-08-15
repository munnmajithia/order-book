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
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ob::book {

class BookError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class ReferenceBook {
  public:
    // Decoded-message application. P does not change the book but flows
    // through so a replay can hand every decoded message to one place; S
    // only tracks the market-hours state used by invariant checking.
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

    // True between the Q (start of market hours) and M (end of market hours)
    // system events. The book can be legitimately crossed outside them.
    [[nodiscard]] bool in_market_hours() const { return market_hours_; }

    // Walks the whole book and throws BookError on the first violation:
    // empty level queues, zero-share resting orders, or an order index that
    // disagrees with the queues. With require_uncrossed, also demands
    // best bid < best ask for every stock with both sides populated —
    // callers pass in_market_hours(), since pre-open books cross legally.
    void check_invariants(bool require_uncrossed) const;

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

    // Invariant sweep of one level; returns the number of resting orders.
    uint64_t check_level(uint16_t locate, char side_tag, uint32_t price, const Queue& queue) const;

    void add_order(uint16_t locate, uint64_t ref, char side, uint32_t shares, uint32_t price);
    void reduce_order(uint64_t ref, uint32_t shares, const char* what);
    void remove_order(uint64_t ref, const char* what);
    OrderLocation& find_order(uint64_t ref, const char* what);
    Queue& queue_at(const OrderLocation& loc);

    std::unordered_map<uint16_t, Side> stocks_;
    std::unordered_map<uint16_t, std::string> names_; // locate -> trimmed symbol
    std::unordered_map<uint64_t, OrderLocation> orders_;
    bool market_hours_ = false;
};

} // namespace ob::book
