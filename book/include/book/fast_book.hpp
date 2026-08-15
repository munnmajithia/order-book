#pragma once

// The fast book: slab-allocated order nodes, intrusive per-level FIFO
// queues, open-addressing level lookup, per-side sorted level lists with the
// best level at the head. Observable behavior must match ReferenceBook
// exactly; cross-validation against it is what makes this book trustworthy.
//
// v1 keeps the layout simple on purpose: sorted-list level insertion walks
// from the touch, and every later optimization must earn its place through
// a profile and before/after numbers (see docs/benchmark-methodology.md).

#include "book/open_map.hpp"
#include "book/reference_book.hpp"
#include "book/slab_pool.hpp"
#include "itch/messages.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace ob::book {

class FastBook {
  public:
    FastBook();

    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): handler interface
    void apply(const itch::SystemEvent& msg) { (void)msg; }
    void apply(const itch::StockDirectory& msg);
    void apply(const itch::AddOrder& msg);
    void apply(const itch::AddOrderMpid& msg) { apply(msg.add); }
    void apply(const itch::OrderExecuted& msg);
    void apply(const itch::OrderExecutedPrice& msg);
    void apply(const itch::OrderCancel& msg);
    void apply(const itch::OrderDelete& msg);
    void apply(const itch::OrderReplace& msg);
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): handler interface
    void apply(const itch::Trade& msg) { (void)msg; }

    [[nodiscard]] std::size_t open_order_count() const { return orders_.size(); }

    // Same canonical format as ReferenceBook::write_snapshot, byte for byte,
    // so cross-validation is plain string equality.
    void write_snapshot(std::ostream& out) const;

  private:
    struct Level;

    struct Order {
        Order* next; // FIFO toward tail; doubles as the pool free-list link
        Order* prev;
        Level* level;
        uint64_t ref;
        uint32_t shares;
    };

    struct Level {
        Level* next; // toward worse prices; doubles as the pool free-list link
        Level* prev;
        Order* head;
        Order* tail;
        uint64_t total_shares;
        uint32_t order_count;
        uint32_t price;
        uint16_t locate;
        char side;
    };

    struct Lists {
        Level* best_bid = nullptr;
        Level* best_ask = nullptr;
    };

    void add_order(uint16_t locate, uint64_t ref, char side, uint32_t shares, uint32_t price);
    void reduce_order(uint64_t ref, uint32_t shares, const char* what);
    void remove_order(uint64_t ref, const char* what);
    Order* find_order(uint64_t ref, const char* what) const;
    Level* level_for(uint16_t locate, char side, uint32_t price);
    void unlink_empty_level(Level* level);

    SlabPool<Order> order_pool_;
    SlabPool<Level> level_pool_;
    OpenMap<Order> orders_;
    OpenMap<Level> levels_;
    std::vector<Lists> stocks_; // indexed by locate
    std::vector<std::string> names_;
};

} // namespace ob::book
