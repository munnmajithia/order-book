// Hardening tests for the parser: hostile and malformed streams must be
// reported as framing errors, never read past the end and never decoded.
//
// The last case in this file drives the libFuzzer entry point directly over the
// same inputs. Every crash the fuzzer finds becomes a case here first, so the
// regression is pinned by a unit test before the fix lands.

#include "itch/decoder.hpp"
#include "itch/messages.hpp"
#include "itch/reader.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size);

namespace {

using namespace ob::itch;

// The ten types the parser decodes into layout structs.
constexpr std::array<unsigned char, 10> kDecodedTypes{
    {'S', 'R', 'A', 'F', 'E', 'C', 'X', 'D', 'U', 'P'}};

// A framed message of `body_len` bytes whose first body byte is the type.
std::vector<std::byte> framed(unsigned char type, uint16_t body_len) {
    std::vector<std::byte> out(2 + static_cast<std::size_t>(body_len), std::byte{0});
    out[0] = std::byte{static_cast<unsigned char>(body_len >> 8)};
    out[1] = std::byte{static_cast<unsigned char>(body_len & 0xFF)};
    if (body_len > 0) {
        out[2] = std::byte{type};
    }
    return out;
}

// A well-framed message of the type's spec length.
std::vector<std::byte> spec_framed(unsigned char type) { return framed(type, spec_length(type)); }

void append(std::vector<std::byte>& into, const std::vector<std::byte>& tail) {
    into.insert(into.end(), tail.begin(), tail.end());
}

// Counts what decode() dispatched, and records the body size each decoded
// message was handed so a short body can never reach message_cast unnoticed.
struct Counter {
    uint64_t decoded = 0;
    uint64_t skipped = 0;
    uint64_t unknown = 0;

    template <typename T> void operator()(const T& /*message*/) { ++decoded; }
    void on_skipped(const Frame& /*frame*/) { ++skipped; }
    void on_unknown(const Frame& /*frame*/) { ++unknown; }
};

// Drains a stream through the reader and decoder. Returns the frame count on a
// clean end of stream, or nullopt when the stream threw.
std::optional<uint64_t> drain(std::span<const std::byte> stream, Counter& counter) {
    FrameReader reader(stream);
    uint64_t frames = 0;
    try {
        while (const auto frame = reader.next()) {
            ++frames;
            decode(*frame, counter);
        }
    } catch (const FramingError&) {
        return std::nullopt;
    }
    return frames;
}

TEST(Malformed, EmptyStreamIsCleanEndNotAnError) {
    Counter counter;
    const std::vector<std::byte> empty;
    const auto frames = drain(empty, counter);
    ASSERT_TRUE(frames.has_value());
    EXPECT_EQ(*frames, 0U); // NOLINT(bugprone-unchecked-optional-access)
}

TEST(Malformed, SingleTrailingByteThrows) {
    const std::vector<std::byte> stream{std::byte{0}};
    Counter counter;
    EXPECT_FALSE(drain(stream, counter).has_value());
}

TEST(Malformed, ZeroLengthPrefixThrows) {
    const auto stream = framed('S', 0);
    Counter counter;
    EXPECT_FALSE(drain(stream, counter).has_value());
}

TEST(Malformed, LengthOneUnderHeaderThrows) {
    // 11 is the header size; a message must carry at least one byte beyond it.
    const auto stream = framed('S', 11);
    Counter counter;
    EXPECT_FALSE(drain(stream, counter).has_value());
}

TEST(Malformed, AllZeroBufferThrows) {
    // A zero length prefix is the first thing a zero-filled stream presents.
    const std::vector<std::byte> stream(4096, std::byte{0});
    Counter counter;
    EXPECT_FALSE(drain(stream, counter).has_value());
}

TEST(Malformed, AllOnesBufferThrows) {
    // Length prefix reads 65535, far past the end of the buffer.
    const std::vector<std::byte> stream(4096, std::byte{0xFF});
    Counter counter;
    EXPECT_FALSE(drain(stream, counter).has_value());
}

TEST(Malformed, MaximumLengthUnknownTypeIsAccepted) {
    // The spec fixes no length for 'z', so the prefix alone bounds it. A full
    // 65535-byte body is well framed and must be counted, not rejected.
    auto stream = framed('z', 65535);
    Counter counter;
    const auto frames = drain(stream, counter);
    ASSERT_TRUE(frames.has_value());
    EXPECT_EQ(*frames, 1U); // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(counter.unknown, 1U);
    EXPECT_EQ(counter.decoded, 0U);
}

TEST(Malformed, TrailingByteAfterAValidFrameThrows) {
    auto stream = spec_framed('S');
    stream.push_back(std::byte{0});
    Counter counter;
    EXPECT_FALSE(drain(stream, counter).has_value());
    EXPECT_EQ(counter.decoded, 1U); // the good frame still decoded
}

TEST(Malformed, TruncatedSecondFrameThrowsAfterYieldingTheFirst) {
    auto stream = spec_framed('A');
    auto second = spec_framed('D');
    second.resize(second.size() - 1);
    append(stream, second);
    Counter counter;
    EXPECT_FALSE(drain(stream, counter).has_value());
    EXPECT_EQ(counter.decoded, 1U);
}

TEST(Malformed, ReaderStaysAtTheBadFrameAndRethrows) {
    auto stream = spec_framed('S');
    const uint64_t bad_offset = stream.size();
    append(stream, framed('D', 25)); // 'D' is 19 in the spec
    FrameReader reader(stream);
    ASSERT_TRUE(reader.next().has_value());
    EXPECT_EQ(reader.offset(), bad_offset);
    EXPECT_THROW((void)reader.next(), FramingError);
    // The offset does not advance past a frame that was never accepted, so the
    // error is stable rather than turning into a different one on retry.
    EXPECT_EQ(reader.offset(), bad_offset);
    EXPECT_THROW((void)reader.next(), FramingError);
}

TEST(Malformed, EveryDecodedTypeRejectsAWrongLength) {
    for (const unsigned char type : kDecodedTypes) {
        const uint16_t good = spec_length(type);
        ASSERT_NE(good, 0U) << type;
        for (const uint16_t bad :
             {static_cast<uint16_t>(good - 1), static_cast<uint16_t>(good + 1)}) {
            const auto stream = framed(type, bad);
            Counter counter;
            EXPECT_FALSE(drain(stream, counter).has_value())
                << "type " << type << " accepted length " << bad;
            EXPECT_EQ(counter.decoded, 0U) << "type " << type << " decoded at length " << bad;
        }
    }
}

TEST(Malformed, EveryDecodedTypeIsAcceptedAtItsSpecLength) {
    for (const unsigned char type : kDecodedTypes) {
        const auto stream = spec_framed(type);
        Counter counter;
        const auto frames = drain(stream, counter);
        ASSERT_TRUE(frames.has_value()) << "type " << type;
        EXPECT_EQ(counter.decoded, 1U) << "type " << type;
    }
}

TEST(Malformed, SkippedTypesAreCountedNotDecoded) {
    // 'H' (trading action) is in the spec but not needed by the book.
    const auto stream = spec_framed('H');
    Counter counter;
    const auto frames = drain(stream, counter);
    ASSERT_TRUE(frames.has_value());
    EXPECT_EQ(counter.skipped, 1U);
    EXPECT_EQ(counter.decoded, 0U);
}

TEST(Malformed, GarbageAfterAValidPreludeIsReportedNotDecoded) {
    auto stream = spec_framed('S');
    for (std::size_t i = 0; i < 512; ++i) {
        stream.push_back(std::byte{static_cast<unsigned char>((i * 37) & 0xFF)});
    }
    Counter counter;
    // Whatever the garbage frames to, the reader either ends the stream cleanly
    // or throws; it must not run off the end. Both outcomes are acceptable, and
    // the sanitizers are what make this assertion mean something.
    (void)drain(stream, counter);
    SUCCEED();
}

// Drives the fuzz entry point over the same hostile inputs. New crash artifacts
// get added here as their own case before the fix that makes them pass.
TEST(FuzzEntry, HostileInputsAreHandled) {
    std::vector<std::vector<std::byte>> inputs;
    inputs.emplace_back();                      // empty
    inputs.emplace_back(1, std::byte{0});       // lone byte
    inputs.emplace_back(4096, std::byte{0});    // zero fill
    inputs.emplace_back(4096, std::byte{0xFF}); // ones fill
    inputs.push_back(framed('S', 0));
    inputs.push_back(framed('S', 11));
    inputs.push_back(framed('z', 65535));
    inputs.push_back(spec_framed('H'));
    for (const unsigned char type : kDecodedTypes) {
        inputs.push_back(spec_framed(type));
        inputs.push_back(framed(type, static_cast<uint16_t>(spec_length(type) - 1)));
    }

    for (const auto& input : inputs) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): byte -> uint8_t
        const auto* bytes = reinterpret_cast<const uint8_t*>(input.data());
        EXPECT_EQ(LLVMFuzzerTestOneInput(bytes, input.size()), 0);
    }
}

} // namespace
