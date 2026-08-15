#include "book/reference_book.hpp"

#include "itch/messages.hpp"

#include <cstdint>
#include <iterator>
#include <map>
#include <ostream>
#include <string>
#include <utility>

namespace ob::book {

namespace {

// FNV-1a over the snapshot text, so the golden file carries a fingerprint.
uint64_t fnv1a(const std::string& text) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace

void ReferenceBook::apply(const itch::SystemEvent& msg) {
    // Q/M bracket regular market hours; the other codes don't matter here.
    if (msg.event_code == 'Q') {
        market_hours_ = true;
    } else if (msg.event_code == 'M') {
        market_hours_ = false;
    }
}

void ReferenceBook::apply(const itch::StockDirectory& msg) {
    names_[static_cast<uint16_t>(msg.header.locate.value())] = std::string(msg.stock.trimmed());
}

void ReferenceBook::apply(const itch::AddOrder& msg) {
    add_order(static_cast<uint16_t>(msg.header.locate.value()), msg.order_ref.value(), msg.side,
              static_cast<uint32_t>(msg.shares.value()), static_cast<uint32_t>(msg.price.value()));
}

void ReferenceBook::apply(const itch::AddOrderMpid& msg) { apply(msg.add); }

void ReferenceBook::apply(const itch::OrderExecuted& msg) {
    reduce_order(msg.order_ref.value(), static_cast<uint32_t>(msg.executed_shares.value()),
                 "order executed");
}

void ReferenceBook::apply(const itch::OrderExecutedPrice& msg) {
    // The printable flag governs trade reporting, not the share reduction.
    reduce_order(msg.executed.order_ref.value(),
                 static_cast<uint32_t>(msg.executed.executed_shares.value()),
                 "order executed with price");
}

void ReferenceBook::apply(const itch::OrderCancel& msg) {
    reduce_order(msg.order_ref.value(), static_cast<uint32_t>(msg.canceled_shares.value()),
                 "order cancel");
}

void ReferenceBook::apply(const itch::OrderDelete& msg) {
    remove_order(msg.order_ref.value(), "order delete");
}

void ReferenceBook::apply(const itch::OrderReplace& msg) {
    // Spec semantics: the replacement is a new order that loses time
    // priority, so replace is exactly delete + add at the back of the new
    // price level, inheriting side and stock from the original.
    const OrderLocation original = find_order(msg.original_ref.value(), "order replace");
    remove_order(msg.original_ref.value(), "order replace");
    add_order(original.locate, msg.new_ref.value(), original.side,
              static_cast<uint32_t>(msg.shares.value()), static_cast<uint32_t>(msg.price.value()));
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void ReferenceBook::apply(const itch::Trade& msg) {
    (void)msg; // non-cross trades execute against hidden interest; no book change
}

ReferenceBook::Queue& ReferenceBook::queue_at(const OrderLocation& loc) {
    Side& side = stocks_.at(loc.locate);
    if (loc.side == 'B') {
        return side.bids.at(loc.price);
    }
    return side.asks.at(loc.price);
}

void ReferenceBook::add_order(uint16_t locate, uint64_t ref, char side, uint32_t shares,
                              uint32_t price) {
    if (orders_.contains(ref)) {
        throw BookError("add order: duplicate ref " + std::to_string(ref));
    }
    if (side != 'B' && side != 'S') {
        throw BookError("add order: bad side for ref " + std::to_string(ref));
    }
    Side& book_side = stocks_[locate];
    Queue& queue = side == 'B' ? book_side.bids[price] : book_side.asks[price];
    queue.push_back(OrderEntry{.ref = ref, .shares = shares});
    orders_.emplace(ref, OrderLocation{.locate = locate,
                                       .side = side,
                                       .price = price,
                                       .position = std::prev(queue.end())});
}

ReferenceBook::OrderLocation& ReferenceBook::find_order(uint64_t ref, const char* what) {
    const auto it = orders_.find(ref);
    if (it == orders_.end()) {
        throw BookError(std::string(what) + ": unknown ref " + std::to_string(ref));
    }
    return it->second;
}

void ReferenceBook::reduce_order(uint64_t ref, uint32_t shares, const char* what) {
    const OrderLocation& loc = find_order(ref, what);
    OrderEntry& entry = *loc.position;
    if (shares > entry.shares) {
        throw BookError(std::string(what) + ": ref " + std::to_string(ref) + " reduces " +
                        std::to_string(shares) + " of " + std::to_string(entry.shares));
    }
    entry.shares -= shares;
    if (entry.shares == 0) {
        remove_order(ref, what);
    }
}

void ReferenceBook::remove_order(uint64_t ref, const char* what) {
    const OrderLocation& loc = find_order(ref, what);
    Queue& queue = queue_at(loc);
    queue.erase(loc.position);
    if (queue.empty()) {
        Side& side = stocks_.at(loc.locate);
        if (loc.side == 'B') {
            side.bids.erase(loc.price);
        } else {
            side.asks.erase(loc.price);
        }
    }
    orders_.erase(ref);
}

uint64_t ReferenceBook::check_level(uint16_t locate, char side_tag, uint32_t price,
                                    const Queue& queue) const {
    if (queue.empty()) {
        throw BookError("invariant: empty level at price " + std::to_string(price) + " locate " +
                        std::to_string(locate));
    }
    uint64_t entries = 0;
    for (const auto& entry : queue) {
        ++entries;
        if (entry.shares == 0) {
            throw BookError("invariant: zero-share resting order ref " + std::to_string(entry.ref));
        }
        const auto it = orders_.find(entry.ref);
        if (it == orders_.end()) {
            throw BookError("invariant: resting ref " + std::to_string(entry.ref) +
                            " missing from index");
        }
        const OrderLocation& loc = it->second;
        if (loc.locate != locate || loc.side != side_tag || loc.price != price ||
            &*loc.position != &entry) {
            throw BookError("invariant: index disagrees with queue for ref " +
                            std::to_string(entry.ref));
        }
    }
    return entries;
}

void ReferenceBook::check_invariants(bool require_uncrossed) const {
    uint64_t entries = 0;
    for (const auto& [locate, side] : stocks_) {
        for (const auto& [price, queue] : side.bids) {
            entries += check_level(locate, 'B', price, queue);
        }
        for (const auto& [price, queue] : side.asks) {
            entries += check_level(locate, 'S', price, queue);
        }
        if (require_uncrossed && !side.bids.empty() && !side.asks.empty()) {
            const uint32_t best_bid = side.bids.begin()->first;
            const uint32_t best_ask = side.asks.begin()->first;
            if (best_bid >= best_ask) {
                throw BookError("invariant: crossed book locate " + std::to_string(locate) +
                                " bid " + std::to_string(best_bid) + " ask " +
                                std::to_string(best_ask));
            }
        }
    }
    if (entries != orders_.size()) {
        throw BookError("invariant: index holds " + std::to_string(orders_.size()) +
                        " orders, queues hold " + std::to_string(entries));
    }
}

void ReferenceBook::write_snapshot(std::ostream& out) const {
    std::string body;
    body += "# end-of-replay book snapshot\n";
    body += "# level lines: side price total_qty order_count (price has 4 implied decimals)\n";

    // Deterministic stock order: by symbol name, locate as tiebreaker.
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
        const auto emit = [&levels, &open_orders](char tag, uint32_t price, const Queue& queue) {
            uint64_t qty = 0;
            for (const auto& entry : queue) {
                qty += entry.shares;
            }
            open_orders += queue.size();
            levels += std::string(1, tag) + " " + std::to_string(price) + " " +
                      std::to_string(qty) + " " + std::to_string(queue.size()) + "\n";
        };
        for (const auto& [price, queue] : side->bids) {
            emit('B', price, queue);
        }
        for (const auto& [price, queue] : side->asks) {
            emit('A', price, queue);
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
