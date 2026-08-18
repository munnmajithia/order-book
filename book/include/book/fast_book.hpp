#pragma once

// Fast book v1: the same observable book as the reference, built on a slab-
// allocated order pool with a free list and intrusive per-level FIFO links.
// Orders are pool indices, not heap nodes, so applying a message never
// allocates once the pool has grown. Price levels are kept in a per-side
// ordered map for v1 (best level is the map front); the level-lookup policy is
// deliberately simple here and left for C3.2 to profile and possibly replace.
//
// It is validated only against the reference book: identical apply semantics,
// and write_snapshot produces byte-identical output, so a replay through both
// can compare state exactly.

#include "itch/messages.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace ob::book {

class FastBookError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class FastBook {
  public:
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

    [[nodiscard]] std::size_t open_order_count() const { return live_orders_; }

    [[nodiscard]] std::optional<uint32_t> best_bid(uint16_t locate) const;
    [[nodiscard]] std::optional<uint32_t> best_ask(uint16_t locate) const;

    // Byte-identical to ReferenceBook::write_snapshot for the same message
    // stream: this is the cross-validation contract.
    void write_snapshot(std::ostream& out) const;

  private:
    static constexpr int32_t kNil = -1;

    // Pool node. next doubles as the free-list link when the node is free.
    struct Order {
        uint64_t ref = 0;
        uint32_t shares = 0;
        int32_t prev = kNil; // intrusive FIFO within its level
        int32_t next = kNil;
    };

    struct Level {
        uint64_t total = 0; // resting shares at this price
        uint32_t count = 0; // resting orders at this price
        int32_t head = kNil;
        int32_t tail = kNil;
    };

    struct Side {
        std::map<uint32_t, Level, std::greater<>> bids;
        std::map<uint32_t, Level, std::less<>> asks;
    };

    // Where an order rests, mirrored from the reference so removal is O(1) into
    // the level maps.
    struct Location {
        uint16_t locate;
        char side;
        uint32_t price;
        int32_t node; // index into pool_
    };

    int32_t alloc_node();
    void free_node(int32_t idx);
    Level& level_for(uint16_t locate, char side, uint32_t price);
    void add_order(uint16_t locate, uint64_t ref, char side, uint32_t shares, uint32_t price);
    void reduce_order(uint64_t ref, uint32_t shares, const char* what);
    void remove_order(uint64_t ref, const char* what);
    Location& find(uint64_t ref, const char* what);

    std::vector<Order> pool_;
    int32_t free_head_ = kNil;
    std::size_t live_orders_ = 0;

    std::unordered_map<uint16_t, Side> stocks_;
    std::unordered_map<uint16_t, std::string> names_;
    std::unordered_map<uint64_t, Location> orders_;
};

} // namespace ob::book
