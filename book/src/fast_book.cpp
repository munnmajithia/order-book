#include "book/fast_book.hpp"

#include "book/reference_book.hpp"
#include "book/snapshot_hash.hpp"
#include "itch/messages.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <utility>

namespace ob::book {

namespace {

constexpr std::size_t kLocateSpace = 1U << 16U;

// Packed nonzero key for the level map: bit 0 salt, price, side, locate.
uint64_t level_key(uint16_t locate, char side, uint32_t price) {
    return 1U | (static_cast<uint64_t>(price) << 1U) | (static_cast<uint64_t>(side == 'S') << 33U) |
           (static_cast<uint64_t>(locate) << 34U);
}

} // namespace

FastBook::FastBook()
    : orders_(1U << 20U), levels_(1U << 18U), stocks_(kLocateSpace), names_(kLocateSpace) {}

void FastBook::apply(const itch::StockDirectory& msg) {
    names_[msg.header.locate.value()] = std::string(msg.stock.trimmed());
}

void FastBook::apply(const itch::AddOrder& msg) {
    add_order(static_cast<uint16_t>(msg.header.locate.value()), msg.order_ref.value(), msg.side,
              static_cast<uint32_t>(msg.shares.value()), static_cast<uint32_t>(msg.price.value()));
}

void FastBook::apply(const itch::OrderExecuted& msg) {
    reduce_order(msg.order_ref.value(), static_cast<uint32_t>(msg.executed_shares.value()),
                 "order executed");
}

void FastBook::apply(const itch::OrderExecutedPrice& msg) {
    reduce_order(msg.executed.order_ref.value(),
                 static_cast<uint32_t>(msg.executed.executed_shares.value()),
                 "order executed with price");
}

void FastBook::apply(const itch::OrderCancel& msg) {
    reduce_order(msg.order_ref.value(), static_cast<uint32_t>(msg.canceled_shares.value()),
                 "order cancel");
}

void FastBook::apply(const itch::OrderDelete& msg) {
    remove_order(msg.order_ref.value(), "order delete");
}

void FastBook::apply(const itch::OrderReplace& msg) {
    // Same semantics as the reference: delete + add at the back of the new
    // level, inheriting side and stock from the original.
    const Order* original = find_order(msg.original_ref.value(), "order replace");
    const Level* level = original->level;
    const uint16_t locate = level->locate;
    const char side = level->side;
    remove_order(msg.original_ref.value(), "order replace");
    add_order(locate, msg.new_ref.value(), side, static_cast<uint32_t>(msg.shares.value()),
              static_cast<uint32_t>(msg.price.value()));
}

FastBook::Order* FastBook::find_order(uint64_t ref, const char* what) const {
    Order* order = orders_.find(ref);
    if (order == nullptr) {
        throw BookError(std::string(what) + ": unknown ref " + std::to_string(ref));
    }
    return order;
}

FastBook::Level* FastBook::level_for(uint16_t locate, char side, uint32_t price) {
    const uint64_t key = level_key(locate, side, price);
    Level* level = levels_.find(key);
    if (level != nullptr) {
        return level;
    }
    level = level_pool_.allocate();
    *level = Level{.next = nullptr,
                   .prev = nullptr,
                   .head = nullptr,
                   .tail = nullptr,
                   .total_shares = 0,
                   .order_count = 0,
                   .price = price,
                   .locate = locate,
                   .side = side};
    levels_.insert(key, level);

    // Walk from the touch to the insertion point. Bids sort high to low,
    // asks low to high; best sits at the list head.
    Lists& lists = stocks_[locate];
    Level*& best = side == 'B' ? lists.best_bid : lists.best_ask;
    Level* after = nullptr; // insert at head when null
    Level* probe = best;
    while (probe != nullptr && (side == 'B' ? probe->price > price : probe->price < price)) {
        after = probe;
        probe = probe->next;
    }
    level->prev = after;
    level->next = probe;
    if (probe != nullptr) {
        probe->prev = level;
    }
    if (after == nullptr) {
        best = level;
    } else {
        after->next = level;
    }
    return level;
}

void FastBook::add_order(uint16_t locate, uint64_t ref, char side, uint32_t shares,
                         uint32_t price) {
    if (ref == 0) {
        throw BookError("add order: ref 0 unsupported"); // 0 is the map's empty marker
    }
    if (orders_.find(ref) != nullptr) {
        throw BookError("add order: duplicate ref " + std::to_string(ref));
    }
    if (side != 'B' && side != 'S') {
        throw BookError("add order: bad side for ref " + std::to_string(ref));
    }
    Level* level = level_for(locate, side, price);
    Order* order = order_pool_.allocate();
    *order =
        Order{.next = nullptr, .prev = level->tail, .level = level, .ref = ref, .shares = shares};
    if (level->tail != nullptr) {
        level->tail->next = order;
    } else {
        level->head = order;
    }
    level->tail = order;
    level->total_shares += shares;
    ++level->order_count;
    orders_.insert(ref, order);
}

void FastBook::reduce_order(uint64_t ref, uint32_t shares, const char* what) {
    Order* order = find_order(ref, what);
    if (shares > order->shares) {
        throw BookError(std::string(what) + ": ref " + std::to_string(ref) + " reduces " +
                        std::to_string(shares) + " of " + std::to_string(order->shares));
    }
    order->shares -= shares;
    order->level->total_shares -= shares;
    if (order->shares == 0) {
        remove_order(ref, what);
    }
}

void FastBook::remove_order(uint64_t ref, const char* what) {
    Order* order = find_order(ref, what);
    Level* level = order->level;
    if (order->prev != nullptr) {
        order->prev->next = order->next;
    } else {
        level->head = order->next;
    }
    if (order->next != nullptr) {
        order->next->prev = order->prev;
    } else {
        level->tail = order->prev;
    }
    level->total_shares -= order->shares;
    --level->order_count;
    if (level->order_count == 0) {
        unlink_empty_level(level);
    }
    orders_.erase(ref);
    order_pool_.release(order);
}

void FastBook::unlink_empty_level(Level* level) {
    Lists& lists = stocks_[level->locate];
    Level*& best = level->side == 'B' ? lists.best_bid : lists.best_ask;
    if (level->prev != nullptr) {
        level->prev->next = level->next;
    } else {
        best = level->next;
    }
    if (level->next != nullptr) {
        level->next->prev = level->prev;
    }
    levels_.erase(level_key(level->locate, level->side, level->price));
    level_pool_.release(level);
}

void FastBook::write_snapshot(std::ostream& out) const {
    std::string body;
    body += "# end-of-replay book snapshot\n";
    body += "# level lines: side price total_qty order_count (price has 4 implied decimals)\n";

    // Same deterministic stock order as the reference: symbol name, then
    // locate as tiebreaker.
    std::map<std::pair<std::string, uint16_t>, const Lists*> ordered;
    for (std::size_t locate = 0; locate < stocks_.size(); ++locate) {
        const Lists& lists = stocks_[locate];
        if (lists.best_bid == nullptr && lists.best_ask == nullptr) {
            continue;
        }
        const std::string& name = names_[locate];
        ordered.emplace(std::make_pair(name.empty() ? "?" : name, static_cast<uint16_t>(locate)),
                        &lists);
    }

    for (const auto& [key, lists] : ordered) {
        uint64_t open_orders = 0;
        uint64_t level_count = 0;
        std::string levels;
        const auto emit = [&levels, &open_orders, &level_count](char tag, const Level* level) {
            for (; level != nullptr; level = level->next) {
                open_orders += level->order_count;
                ++level_count;
                levels += std::string(1, tag) + " " + std::to_string(level->price) + " " +
                          std::to_string(level->total_shares) + " " +
                          std::to_string(level->order_count) + "\n";
            }
        };
        emit('B', lists->best_bid);
        emit('A', lists->best_ask);
        body += "stock " + key.first + " locate " + std::to_string(key.second) + " levels " +
                std::to_string(level_count) + " orders " + std::to_string(open_orders) + "\n";
        body += levels;
    }
    body += "open_orders_total " + std::to_string(orders_.size()) + "\n";

    out << body;
    out << "fnv1a " << fnv1a(body) << "\n";
}

} // namespace ob::book
