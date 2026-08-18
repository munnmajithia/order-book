#include "book/fast_book.hpp"

#include "itch/messages.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <utility>

namespace ob::book {

namespace {

// FNV-1a over the snapshot text; identical to the reference book so the two
// snapshots hash the same when the books agree.
uint64_t fnv1a(const std::string& text) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace

int32_t FastBook::alloc_node() {
    if (free_head_ != kNil) {
        const int32_t idx = free_head_;
        free_head_ = pool_[static_cast<std::size_t>(idx)].next;
        pool_[static_cast<std::size_t>(idx)] = Order{};
        return idx;
    }
    pool_.push_back(Order{});
    return static_cast<int32_t>(pool_.size() - 1);
}

void FastBook::free_node(int32_t idx) {
    pool_[static_cast<std::size_t>(idx)] = Order{};
    pool_[static_cast<std::size_t>(idx)].next = free_head_;
    free_head_ = idx;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): apply interface
void FastBook::apply(const itch::SystemEvent& msg) { (void)msg; }

void FastBook::apply(const itch::StockDirectory& msg) {
    names_[static_cast<uint16_t>(msg.header.locate.value())] = std::string(msg.stock.trimmed());
}

void FastBook::apply(const itch::AddOrder& msg) {
    add_order(static_cast<uint16_t>(msg.header.locate.value()), msg.order_ref.value(), msg.side,
              static_cast<uint32_t>(msg.shares.value()), static_cast<uint32_t>(msg.price.value()));
}

void FastBook::apply(const itch::AddOrderMpid& msg) { apply(msg.add); }

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
    // Delete + add at the back of the new level, inheriting side and stock,
    // exactly as the reference book does.
    const Location original = find(msg.original_ref.value(), "order replace");
    const uint16_t locate = original.locate;
    const char side = original.side;
    remove_order(msg.original_ref.value(), "order replace");
    add_order(locate, msg.new_ref.value(), side, static_cast<uint32_t>(msg.shares.value()),
              static_cast<uint32_t>(msg.price.value()));
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): apply interface
void FastBook::apply(const itch::Trade& msg) { (void)msg; }

FastBook::Level& FastBook::level_for(uint16_t locate, char side, uint32_t price) {
    Side& book_side = stocks_[locate];
    return side == 'B' ? book_side.bids[price] : book_side.asks[price];
}

void FastBook::add_order(uint16_t locate, uint64_t ref, char side, uint32_t shares,
                         uint32_t price) {
    if (orders_.contains(ref)) {
        throw FastBookError("add order: duplicate ref " + std::to_string(ref));
    }
    if (side != 'B' && side != 'S') {
        throw FastBookError("add order: bad side for ref " + std::to_string(ref));
    }
    const int32_t node = alloc_node();
    Order& order = pool_[static_cast<std::size_t>(node)];
    order.ref = ref;
    order.shares = shares;
    order.prev = kNil;
    order.next = kNil;

    Level& level = level_for(locate, side, price);
    if (level.tail == kNil) {
        level.head = node;
        level.tail = node;
    } else {
        order.prev = level.tail;
        pool_[static_cast<std::size_t>(level.tail)].next = node;
        level.tail = node;
    }
    level.total += shares;
    ++level.count;

    orders_.emplace(ref, Location{.locate = locate, .side = side, .price = price, .node = node});
    ++live_orders_;
}

FastBook::Location& FastBook::find(uint64_t ref, const char* what) {
    const auto it = orders_.find(ref);
    if (it == orders_.end()) {
        throw FastBookError(std::string(what) + ": unknown ref " + std::to_string(ref));
    }
    return it->second;
}

void FastBook::reduce_order(uint64_t ref, uint32_t shares, const char* what) {
    const Location& loc = find(ref, what);
    Order& order = pool_[static_cast<std::size_t>(loc.node)];
    if (shares > order.shares) {
        throw FastBookError(std::string(what) + ": ref " + std::to_string(ref) + " reduces " +
                            std::to_string(shares) + " of " + std::to_string(order.shares));
    }
    order.shares -= shares;
    Level& level = level_for(loc.locate, loc.side, loc.price);
    level.total -= shares;
    if (order.shares == 0) {
        remove_order(ref, what);
    }
}

void FastBook::remove_order(uint64_t ref, const char* what) {
    const Location& loc = find(ref, what);
    const int32_t node = loc.node;
    Order& order = pool_[static_cast<std::size_t>(node)];

    Side& book_side = stocks_.at(loc.locate);
    const auto detach = [&](auto& levels) {
        auto lit = levels.find(loc.price);
        Level& level = lit->second;
        const int32_t prev = order.prev;
        const int32_t next = order.next;
        if (prev != kNil) {
            pool_[static_cast<std::size_t>(prev)].next = next;
        } else {
            level.head = next;
        }
        if (next != kNil) {
            pool_[static_cast<std::size_t>(next)].prev = prev;
        } else {
            level.tail = prev;
        }
        // Whatever the order still had leaves the level total. On the reduce
        // path its shares are already zero, so this is a no-op there; on a
        // delete it removes the resting remainder.
        level.total -= order.shares;
        --level.count;
        if (level.count == 0) {
            levels.erase(lit);
        }
    };
    if (loc.side == 'B') {
        detach(book_side.bids);
    } else {
        detach(book_side.asks);
    }

    orders_.erase(ref);
    free_node(node);
    --live_orders_;
    (void)what;
}

std::optional<uint32_t> FastBook::best_bid(uint16_t locate) const {
    const auto it = stocks_.find(locate);
    if (it == stocks_.end() || it->second.bids.empty()) {
        return std::nullopt;
    }
    return it->second.bids.begin()->first;
}

std::optional<uint32_t> FastBook::best_ask(uint16_t locate) const {
    const auto it = stocks_.find(locate);
    if (it == stocks_.end() || it->second.asks.empty()) {
        return std::nullopt;
    }
    return it->second.asks.begin()->first;
}

void FastBook::write_snapshot(std::ostream& out) const {
    std::string body;
    body += "# end-of-replay book snapshot\n";
    body += "# level lines: side price total_qty order_count (price has 4 implied decimals)\n";

    std::map<std::pair<std::string, uint16_t>, const Side*> ordered;
    for (const auto& [locate, side] : stocks_) {
        if (side.bids.empty() && side.asks.empty()) {
            continue;
        }
        const auto name_it = names_.find(locate);
        const std::string name = name_it != names_.end() ? name_it->second : "?";
        ordered.emplace(std::make_pair(name, locate), &side);
    }

    for (const auto& [key, side] : ordered) {
        uint64_t open_orders = 0;
        std::string levels;
        const auto emit = [&levels, &open_orders](char tag, uint32_t price, const Level& level) {
            open_orders += level.count;
            levels += std::string(1, tag) + " " + std::to_string(price) + " " +
                      std::to_string(level.total) + " " + std::to_string(level.count) + "\n";
        };
        for (const auto& [price, level] : side->bids) {
            emit('B', price, level);
        }
        for (const auto& [price, level] : side->asks) {
            emit('A', price, level);
        }
        body += "stock " + key.first + " locate " + std::to_string(key.second) + " levels " +
                std::to_string(side->bids.size() + side->asks.size()) + " orders " +
                std::to_string(open_orders) + "\n";
        body += levels;
    }
    body += "open_orders_total " + std::to_string(orders_.size()) + "\n";

    out << body;
    out << "fnv1a " << fnv1a(body) << "\n";
}

} // namespace ob::book
