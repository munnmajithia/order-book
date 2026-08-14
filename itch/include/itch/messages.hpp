#pragma once

// NASDAQ TotalView-ITCH 5.0 message layouts for the book-relevant types
// (S R A F E C X D U P), as packed byte-for-byte structs.
//
// No packing pragmas: every field is a char or a byte array, so each struct
// has alignment 1 and no padding by construction. The static_asserts below
// hold every size to the spec, and the zero-copy cast in message_cast is
// alignment-safe by definition. Big-endian fields decode through a single
// uint64 accumulator, which keeps -Wconversion quiet and the codegen simple.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace ob::itch {

template <std::size_t N>
    requires(N >= 1 && N <= 8)
struct BeInt {
    std::array<unsigned char, N> bytes;

    [[nodiscard]] constexpr uint64_t value() const {
        uint64_t acc = 0;
        for (const unsigned char b : bytes) {
            acc = (acc << 8) | b;
        }
        return acc;
    }
};

using Be16 = BeInt<2>;
using Be32 = BeInt<4>;
using Be48 = BeInt<6>;
using Be64 = BeInt<8>;

// Space-padded left-justified alpha field.
template <std::size_t N> struct Alpha {
    std::array<char, N> chars;

    [[nodiscard]] constexpr std::string_view padded() const { return {chars.data(), N}; }

    [[nodiscard]] constexpr std::string_view trimmed() const {
        std::string_view v{chars.data(), N};
        while (!v.empty() && v.back() == ' ') {
            v.remove_suffix(1);
        }
        return v;
    }
};

// Common 11-byte header carried by every ITCH 5.0 message.
struct Header {
    char type;
    Be16 locate;
    Be16 tracking;
    Be48 timestamp; // nanoseconds since midnight
};

struct SystemEvent { // S
    Header header;
    char event_code;
};

struct StockDirectory { // R
    Header header;
    Alpha<8> stock;
    char market_category;
    char financial_status;
    Be32 round_lot_size;
    char round_lots_only;
    char issue_classification;
    Alpha<2> issue_subtype;
    char authenticity;
    char short_sale_threshold;
    char ipo_flag;
    char luld_reference_tier;
    char etp_flag;
    Be32 etp_leverage_factor;
    char inverse_indicator;
};

struct AddOrder { // A
    Header header;
    Be64 order_ref;
    char side; // 'B' or 'S'
    Be32 shares;
    Alpha<8> stock;
    Be32 price; // 4 implied decimals
};

struct AddOrderMpid { // F
    AddOrder add;
    Alpha<4> mpid;
};

struct OrderExecuted { // E
    Header header;
    Be64 order_ref;
    Be32 executed_shares;
    Be64 match_number;
};

struct OrderExecutedPrice { // C
    OrderExecuted executed;
    char printable;
    Be32 execution_price;
};

struct OrderCancel { // X
    Header header;
    Be64 order_ref;
    Be32 canceled_shares;
};

struct OrderDelete { // D
    Header header;
    Be64 order_ref;
};

struct OrderReplace { // U
    Header header;
    Be64 original_ref;
    Be64 new_ref;
    Be32 shares;
    Be32 price;
};

struct Trade { // P — non-cross trade against a hidden order; no book impact
    Header header;
    Be64 order_ref;
    char side;
    Be32 shares;
    Alpha<8> stock;
    Be32 price;
    Be64 match_number;
};

static_assert(sizeof(Header) == 11);
static_assert(sizeof(SystemEvent) == 12);
static_assert(sizeof(StockDirectory) == 39);
static_assert(sizeof(AddOrder) == 36);
static_assert(sizeof(AddOrderMpid) == 40);
static_assert(sizeof(OrderExecuted) == 31);
static_assert(sizeof(OrderExecutedPrice) == 36);
static_assert(sizeof(OrderCancel) == 23);
static_assert(sizeof(OrderDelete) == 19);
static_assert(sizeof(OrderReplace) == 35);
static_assert(sizeof(Trade) == 44);

template <typename T>
concept RawMessage = std::is_trivially_copyable_v<T> && alignof(T) == 1;

static_assert(RawMessage<SystemEvent> && RawMessage<StockDirectory> && RawMessage<AddOrder> &&
              RawMessage<AddOrderMpid> && RawMessage<OrderExecuted> &&
              RawMessage<OrderExecutedPrice> && RawMessage<OrderCancel> &&
              RawMessage<OrderDelete> && RawMessage<OrderReplace> && RawMessage<Trade>);

// Zero-copy view of a framed message body as its layout struct. The concept
// constrains T to trivially copyable, alignment-1 layouts, so the cast cannot
// produce a misaligned access. std::start_lifetime_as would be the fully
// blessed spelling, but neither libstdc++ 13 nor 18 implements it yet; this
// is the one confined reinterpret_cast in the parser.
template <RawMessage T> const T& message_cast(std::span<const std::byte> body) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return *reinterpret_cast<const T*>(body.data());
}

// Total message length (type byte included) the spec fixes for each type;
// 0 for types the spec does not define.
[[nodiscard]] constexpr uint16_t spec_length(unsigned char type) {
    constexpr std::array<uint16_t, 256> kTable = [] {
        std::array<uint16_t, 256> t{};
        t['S'] = 12;
        t['R'] = 39;
        t['H'] = 25;
        t['Y'] = 20;
        t['L'] = 26;
        t['V'] = 35;
        t['W'] = 12;
        t['K'] = 28;
        t['J'] = 35;
        t['h'] = 21;
        t['A'] = 36;
        t['F'] = 40;
        t['E'] = 31;
        t['C'] = 36;
        t['X'] = 23;
        t['D'] = 19;
        t['U'] = 35;
        t['P'] = 44;
        t['Q'] = 40;
        t['B'] = 19;
        t['I'] = 50;
        t['N'] = 20;
        t['O'] = 48;
        return t;
    }();
    return kTable[type];
}

// Three-way partition of the type space: types this parser decodes into
// structs, types the spec defines that the book does not need (counted and
// skipped), and types outside the spec entirely (counted and skipped, but
// their presence in a captured stream is worth noticing).
enum class MessageClass : uint8_t { decoded, skipped, unknown };

[[nodiscard]] constexpr MessageClass classify(unsigned char type) {
    switch (type) {
    case 'S':
    case 'R':
    case 'A':
    case 'F':
    case 'E':
    case 'C':
    case 'X':
    case 'D':
    case 'U':
    case 'P':
        return MessageClass::decoded;
    default:
        return spec_length(type) != 0 ? MessageClass::skipped : MessageClass::unknown;
    }
}

} // namespace ob::itch
