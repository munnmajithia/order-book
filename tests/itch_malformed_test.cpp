// Hostile-input coverage for the parser, beyond the basic throw paths in
// reader_fixture_test.cpp: error offsets in the exception text, framing
// boundary cases (minimum and maximum lengths), decode dispatch over
// adversarial field values, and deterministic pseudo-random garbage streams
// exercising the same contract the fuzzer enforces — parse cleanly or throw
// FramingError, never anything else.
#include "itch/decoder.hpp"
#include "itch/messages.hpp"
#include "itch/reader.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace ob::itch;

std::vector<std::byte> frame_bytes(unsigned char type, uint16_t body_len,
                                   std::byte fill = std::byte{0}) {
    std::vector<std::byte> out(2 + static_cast<std::size_t>(body_len), fill);
    out[0] = std::byte{static_cast<unsigned char>(body_len >> 8)};
    out[1] = std::byte{static_cast<unsigned char>(body_len & 0xFF)};
    if (body_len > 0) {
        out[2] = std::byte{type};
    }
    return out;
}

struct CountingHandler {
    uint64_t decoded = 0;
    uint64_t skipped = 0;
    uint64_t unknown = 0;
    uint64_t field_sum = 0; // folds field reads so none are optimized away

    void operator()(const SystemEvent& m) {
        ++decoded;
        field_sum += m.header.timestamp.value() + static_cast<unsigned char>(m.event_code);
    }
    void operator()(const StockDirectory& m) {
        ++decoded;
        field_sum +=
            m.round_lot_size.value() + m.etp_leverage_factor.value() + m.stock.trimmed().size();
    }
    void operator()(const AddOrder& m) {
        ++decoded;
        field_sum += m.order_ref.value() + m.shares.value() + m.price.value() +
                     m.stock.trimmed().size() + static_cast<unsigned char>(m.side);
    }
    void operator()(const AddOrderMpid& m) {
        (*this)(m.add);
        field_sum += m.mpid.trimmed().size();
    }
    void operator()(const OrderExecuted& m) {
        ++decoded;
        field_sum += m.order_ref.value() + m.executed_shares.value() + m.match_number.value();
    }
    void operator()(const OrderExecutedPrice& m) {
        (*this)(m.executed);
        field_sum += m.execution_price.value() + static_cast<unsigned char>(m.printable);
    }
    void operator()(const OrderCancel& m) {
        ++decoded;
        field_sum += m.order_ref.value() + m.canceled_shares.value();
    }
    void operator()(const OrderDelete& m) {
        ++decoded;
        field_sum += m.order_ref.value();
    }
    void operator()(const OrderReplace& m) {
        ++decoded;
        field_sum +=
            m.original_ref.value() + m.new_ref.value() + m.shares.value() + m.price.value();
    }
    void operator()(const Trade& m) {
        ++decoded;
        field_sum +=
            m.order_ref.value() + m.shares.value() + m.price.value() + m.match_number.value();
    }
    void on_skipped(const Frame& frame) {
        ++skipped;
        field_sum += frame.body.size();
    }
    void on_unknown(const Frame& frame) {
        ++unknown;
        field_sum += frame.body.size();
    }
};

std::string framing_error_text(std::vector<std::byte> stream) {
    FrameReader reader(stream);
    try {
        while (reader.next()) {
        }
    } catch (const FramingError& e) {
        return e.what();
    }
    ADD_FAILURE() << "stream parsed without FramingError";
    return {};
}

TEST(MalformedFraming, ErrorCarriesOffsetOfBadFrame) {
    // One valid S frame (14 bytes on the wire), then a bad length prefix.
    auto stream = frame_bytes('S', 12);
    const auto bad = frame_bytes('S', 5);
    stream.insert(stream.end(), bad.begin(), bad.end());
    EXPECT_NE(framing_error_text(stream).find("offset 14"), std::string::npos);
}

TEST(MalformedFraming, TruncatedPrefixOffsetAfterValidFrame) {
    auto stream = frame_bytes('D', 19);
    stream.push_back(std::byte{0}); // lone half of a length prefix
    EXPECT_NE(framing_error_text(stream).find("offset 21"), std::string::npos);
}

TEST(MalformedFraming, ZeroLengthRejected) {
    const auto stream = frame_bytes('S', 0);
    EXPECT_NE(framing_error_text(stream).find("length 0"), std::string::npos);
}

TEST(MalformedFraming, MinimumLengthUnknownTypeAccepted) {
    // 12 = header + type byte is the smallest well-formed frame.
    const auto stream = frame_bytes('z', 12);
    FrameReader reader(stream);
    const auto frame = reader.next();
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->body.size(), 12U); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_FALSE(reader.next().has_value());
}

TEST(MalformedFraming, MaximumLengthUnknownTypeAccepted) {
    const auto stream = frame_bytes('z', 65535);
    FrameReader reader(stream);
    const auto frame = reader.next();
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->body.size(), 65535U); // NOLINT(bugprone-unchecked-optional-access)
}

TEST(MalformedFraming, MaximumLengthTruncatedThrows) {
    auto stream = frame_bytes('z', 65535);
    stream.pop_back();
    EXPECT_NE(framing_error_text(stream).find("truncated message"), std::string::npos);
}

TEST(MalformedFraming, SkippedSpecTypeLengthEnforced) {
    // 'H' is spec-defined at 25 bytes even though the book never decodes it.
    const auto stream = frame_bytes('H', 30);
    EXPECT_NE(framing_error_text(stream).find("!= spec 25"), std::string::npos);
}

TEST(HostileDecode, AllOnesFieldsDecodeToMaxValues) {
    CountingHandler handler;
    constexpr std::array<unsigned char, 10> kDecodedTypes{'S', 'R', 'A', 'F', 'E',
                                                          'C', 'X', 'D', 'U', 'P'};
    for (const unsigned char type : kDecodedTypes) {
        auto stream = frame_bytes(type, spec_length(type), std::byte{0xFF});
        stream[2] = std::byte{type}; // fill overwrote the type byte
        FrameReader reader(stream);
        const auto frame = reader.next();
        ASSERT_TRUE(frame.has_value());
        decode(*frame, handler); // NOLINT(bugprone-unchecked-optional-access)
    }
    EXPECT_EQ(handler.decoded, 10U);
    EXPECT_EQ(handler.skipped, 0U);
    EXPECT_EQ(handler.unknown, 0U);

    // Spot-check one layout end to end: an all-ones D message.
    const auto stream = frame_bytes('D', 19, std::byte{0xFF});
    FrameReader reader(stream);
    const auto frame = reader.next();
    ASSERT_TRUE(frame.has_value());
    // has_value asserted above; the checker cannot see through ASSERT_TRUE
    const auto& del =
        message_cast<OrderDelete>(frame->body); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(del.order_ref.value(), UINT64_MAX);
    EXPECT_EQ(del.header.timestamp.value(), 0xFFFF'FFFF'FFFFU);
}

TEST(HostileDecode, SkippedAndUnknownDispatch) {
    CountingHandler handler;
    auto stream = frame_bytes('H', 25); // spec-defined, not decoded
    const auto unknown = frame_bytes('q', 40);
    stream.insert(stream.end(), unknown.begin(), unknown.end());
    FrameReader reader(stream);
    while (const auto frame = reader.next()) {
        decode(*frame, handler);
    }
    EXPECT_EQ(handler.decoded, 0U);
    EXPECT_EQ(handler.skipped, 1U);
    EXPECT_EQ(handler.unknown, 1U);
}

TEST(HostileDecode, AllSpacesAlphaTrimsEmpty) {
    auto stream = frame_bytes('A', 36, std::byte{' '});
    stream[2] = std::byte{'A'};
    FrameReader reader(stream);
    const auto frame = reader.next();
    ASSERT_TRUE(frame.has_value());
    // has_value asserted above; the checker cannot see through ASSERT_TRUE
    const auto& add =
        message_cast<AddOrder>(frame->body); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(add.stock.trimmed(), "");
    EXPECT_EQ(add.stock.padded().size(), 8U);
}

// The fuzzer's contract, replayed on deterministic garbage: every input either
// parses to the end or throws FramingError. Under ASan/UBSan in CI this doubles
// as a cheap sanitizer sweep of the parse path.
TEST(GarbageStreams, ParseCleanlyOrThrowFramingError) {
    uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto next_byte = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return static_cast<std::byte>(state & 0xFF);
    };
    uint64_t threw = 0;
    for (int round = 0; round < 256; ++round) {
        std::vector<std::byte> stream(static_cast<std::size_t>(round) * 7 + 1);
        for (auto& b : stream) {
            b = next_byte();
        }
        FrameReader reader(stream);
        CountingHandler handler;
        try {
            while (const auto frame = reader.next()) {
                decode(*frame, handler);
            }
        } catch (const FramingError&) {
            ++threw;
        }
        EXPECT_LE(reader.offset(), stream.size());
    }
    EXPECT_GT(threw, 0U); // random bytes must not all frame cleanly
}

} // namespace
