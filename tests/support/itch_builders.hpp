#pragma once

// Hand-built ITCH messages for book tests. A locate argument lets a test drive
// more than one stock through the same book; it defaults to 1 so the simple
// single-stock tests read the same as before this header existed.

#include "itch/messages.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ob::test {

template <std::size_t N> itch::BeInt<N> be(uint64_t v) {
    itch::BeInt<N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out.bytes[N - 1 - i] = static_cast<unsigned char>(v & 0xFFU);
        v >>= 8U;
    }
    return out;
}

// A stock symbol from a locate: "SYM00001" style, so distinct locates get
// distinct names in a snapshot without a lookup table.
inline itch::Alpha<8> stock_for(uint16_t locate) {
    std::array<char, 8> chars{'S', 'Y', 'M', '0', '0', '0', '0', '0'};
    uint16_t n = locate;
    for (std::size_t i = 8; i-- > 3;) {
        chars[i] = static_cast<char>('0' + (n % 10));
        n /= 10;
    }
    return itch::Alpha<8>{chars};
}

inline itch::StockDirectory directory(uint16_t locate) {
    itch::StockDirectory msg{};
    msg.header.type = 'R';
    msg.header.locate = be<2>(locate);
    msg.stock = stock_for(locate);
    return msg;
}

inline itch::AddOrder add(uint64_t ref, char side, uint32_t shares, uint32_t price,
                          uint16_t locate = 1) {
    itch::AddOrder msg{};
    msg.header.type = 'A';
    msg.header.locate = be<2>(locate);
    msg.order_ref = be<8>(ref);
    msg.side = side;
    msg.shares = be<4>(shares);
    msg.stock = stock_for(locate);
    msg.price = be<4>(price);
    return msg;
}

inline itch::OrderExecuted executed(uint64_t ref, uint32_t shares) {
    itch::OrderExecuted msg{};
    msg.header.type = 'E';
    msg.order_ref = be<8>(ref);
    msg.executed_shares = be<4>(shares);
    return msg;
}

inline itch::OrderCancel cancel(uint64_t ref, uint32_t shares) {
    itch::OrderCancel msg{};
    msg.header.type = 'X';
    msg.order_ref = be<8>(ref);
    msg.canceled_shares = be<4>(shares);
    return msg;
}

inline itch::OrderDelete del(uint64_t ref) {
    itch::OrderDelete msg{};
    msg.header.type = 'D';
    msg.order_ref = be<8>(ref);
    return msg;
}

inline itch::OrderReplace replace(uint64_t orig, uint64_t next, uint32_t shares, uint32_t price) {
    itch::OrderReplace msg{};
    msg.header.type = 'U';
    msg.original_ref = be<8>(orig);
    msg.new_ref = be<8>(next);
    msg.shares = be<4>(shares);
    msg.price = be<4>(price);
    return msg;
}

} // namespace ob::test
