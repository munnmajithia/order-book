#pragma once

// Zero-copy decode dispatch: switch on the type byte, view the frame body as
// its layout struct, hand it to the handler. The handler provides an overload
// (or template) accepting each decoded struct; skipped and unknown types are
// reported through on_skipped/on_unknown so a caller can count them.

#include "itch/messages.hpp"
#include "itch/reader.hpp"

namespace ob::itch {

template <typename H>
concept DecodeHandler = requires(H h, const Frame& f) {
    h(SystemEvent{});
    h(StockDirectory{});
    h(AddOrder{});
    h(AddOrderMpid{});
    h(OrderExecuted{});
    h(OrderExecutedPrice{});
    h(OrderCancel{});
    h(OrderDelete{});
    h(OrderReplace{});
    h(Trade{});
    h.on_skipped(f);
    h.on_unknown(f);
};

template <DecodeHandler H> void decode(const Frame& frame, H& handler) {
    switch (frame.type()) {
    case 'S':
        handler(message_cast<SystemEvent>(frame.body));
        break;
    case 'R':
        handler(message_cast<StockDirectory>(frame.body));
        break;
    case 'A':
        handler(message_cast<AddOrder>(frame.body));
        break;
    case 'F':
        handler(message_cast<AddOrderMpid>(frame.body));
        break;
    case 'E':
        handler(message_cast<OrderExecuted>(frame.body));
        break;
    case 'C':
        handler(message_cast<OrderExecutedPrice>(frame.body));
        break;
    case 'X':
        handler(message_cast<OrderCancel>(frame.body));
        break;
    case 'D':
        handler(message_cast<OrderDelete>(frame.body));
        break;
    case 'U':
        handler(message_cast<OrderReplace>(frame.body));
        break;
    case 'P':
        handler(message_cast<Trade>(frame.body));
        break;
    default:
        if (classify(frame.type()) == MessageClass::skipped) {
            handler.on_skipped(frame);
        } else {
            handler.on_unknown(frame);
        }
        break;
    }
}

} // namespace ob::itch
