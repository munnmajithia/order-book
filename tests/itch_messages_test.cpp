// Message layout invariants: sizes against the spec, big-endian decode
// through the accumulator, alpha trimming, and the three-way type partition.
#include "itch/messages.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>

namespace {

using namespace ob::itch;

TEST(Messages, SizesMatchSpec) {
    // Compile-time in the header; restated here so a failure names the type.
    EXPECT_EQ(sizeof(SystemEvent), 12U);
    EXPECT_EQ(sizeof(StockDirectory), 39U);
    EXPECT_EQ(sizeof(AddOrder), 36U);
    EXPECT_EQ(sizeof(AddOrderMpid), 40U);
    EXPECT_EQ(sizeof(OrderExecuted), 31U);
    EXPECT_EQ(sizeof(OrderExecutedPrice), 36U);
    EXPECT_EQ(sizeof(OrderCancel), 23U);
    EXPECT_EQ(sizeof(OrderDelete), 19U);
    EXPECT_EQ(sizeof(OrderReplace), 35U);
    EXPECT_EQ(sizeof(Trade), 44U);
}

TEST(Messages, BigEndianDecode) {
    const Be16 two{{0x01, 0x02}};
    EXPECT_EQ(two.value(), 0x0102U);
    const Be32 four{{0xDE, 0xAD, 0xBE, 0xEF}};
    EXPECT_EQ(four.value(), 0xDEADBEEFU);
    const Be48 six{{0x01, 0x02, 0x03, 0x04, 0x05, 0x06}};
    EXPECT_EQ(six.value(), 0x010203040506ULL);
    const Be64 eight{{0xFF, 0, 0, 0, 0, 0, 0, 0x01}};
    EXPECT_EQ(eight.value(), 0xFF00000000000001ULL);
}

TEST(Messages, AlphaTrimsRightPadding) {
    const Alpha<8> stock{{'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '}};
    EXPECT_EQ(stock.trimmed(), "AAPL");
    EXPECT_EQ(stock.padded().size(), 8U);
}

TEST(Messages, MessageCastViewsRawBytes) {
    std::array<std::byte, sizeof(AddOrder)> raw{};
    raw[0] = std::byte{'A'};
    raw[1] = std::byte{0x00}; // locate 7
    raw[2] = std::byte{0x07};
    raw[11] = std::byte{0x00}; // order ref: last byte 42
    raw[18] = std::byte{42};
    raw[19] = std::byte{'B'};
    raw[23] = std::byte{100}; // shares (low byte)
    const auto& msg = message_cast<AddOrder>(std::span<const std::byte>(raw));
    EXPECT_EQ(msg.header.type, 'A');
    EXPECT_EQ(msg.header.locate.value(), 7U);
    EXPECT_EQ(msg.order_ref.value(), 42U);
    EXPECT_EQ(msg.side, 'B');
    EXPECT_EQ(msg.shares.value(), 100U);
}

TEST(Messages, ClassifyPartitionsTypeSpace) {
    for (const char t : {'S', 'R', 'A', 'F', 'E', 'C', 'X', 'D', 'U', 'P'}) {
        EXPECT_EQ(classify(static_cast<unsigned char>(t)), MessageClass::decoded) << t;
    }
    for (const char t : {'H', 'Y', 'L', 'V', 'W', 'K', 'J', 'h', 'Q', 'B', 'I', 'N', 'O'}) {
        EXPECT_EQ(classify(static_cast<unsigned char>(t)), MessageClass::skipped) << t;
    }
    for (const unsigned char t : std::array<unsigned char, 5>{
             static_cast<unsigned char>('Z'), static_cast<unsigned char>('a'),
             static_cast<unsigned char>('0'), 0x00, 0xFF}) {
        EXPECT_EQ(classify(t), MessageClass::unknown) << static_cast<int>(t);
    }
}

TEST(Messages, SpecLengthsForDecodedTypes) {
    EXPECT_EQ(spec_length('S'), 12U);
    EXPECT_EQ(spec_length('R'), 39U);
    EXPECT_EQ(spec_length('A'), 36U);
    EXPECT_EQ(spec_length('F'), 40U);
    EXPECT_EQ(spec_length('E'), 31U);
    EXPECT_EQ(spec_length('C'), 36U);
    EXPECT_EQ(spec_length('X'), 23U);
    EXPECT_EQ(spec_length('D'), 19U);
    EXPECT_EQ(spec_length('U'), 35U);
    EXPECT_EQ(spec_length('P'), 44U);
    EXPECT_EQ(spec_length('Z'), 0U);
}

} // namespace
