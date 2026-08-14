// Framing over the committed CI fixture: totals, class split, monotone
// timestamps. Synthetic streams cover the throw conditions and the
// unknown-type path, which the fixture deliberately does not contain.
#include "itch/messages.hpp"
#include "itch/reader.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace ob::itch;

std::string fixture_path() { return std::string(OB_TEST_DATA_DIR) + "/fixture.itch"; }

TEST(ReaderFixture, FramingTotalsAndClassSplit) {
    const MappedFile file(fixture_path());
    ASSERT_EQ(file.bytes().size(), 5'242'875U);

    FrameReader reader(file.bytes());
    uint64_t frames = 0;
    uint64_t decoded = 0;
    uint64_t skipped = 0;
    uint64_t unknown = 0;
    uint64_t last_timestamp = 0;
    bool timestamps_monotone = true;
    while (const auto frame = reader.next()) {
        ++frames;
        switch (classify(frame->type())) {
        case MessageClass::decoded:
            ++decoded;
            break;
        case MessageClass::skipped:
            ++skipped;
            break;
        case MessageClass::unknown:
            ++unknown;
            break;
        }
        // The fixture is a subsequence of one day's feed, so timestamps
        // never decrease. If a future fixture violates this, relax to
        // per-message in-day bounds rather than deleting the check.
        const auto& header = message_cast<Header>(frame->body);
        const uint64_t ts = header.timestamp.value();
        timestamps_monotone = timestamps_monotone && ts >= last_timestamp;
        last_timestamp = ts;
    }
    EXPECT_EQ(frames, 152'005U);
    EXPECT_EQ(decoded, 151'476U);
    EXPECT_EQ(skipped, 529U);
    EXPECT_EQ(unknown, 0U);
    EXPECT_EQ(reader.offset(), file.bytes().size());
    EXPECT_TRUE(timestamps_monotone);
}

std::vector<std::byte> frame_bytes(unsigned char type, uint16_t body_len) {
    std::vector<std::byte> out(2 + static_cast<std::size_t>(body_len), std::byte{0});
    out[0] = std::byte{static_cast<unsigned char>(body_len >> 8)};
    out[1] = std::byte{static_cast<unsigned char>(body_len & 0xFF)};
    out[2] = std::byte{type};
    return out;
}

TEST(Reader, CleanEndOfStream) {
    const auto stream = frame_bytes('S', 12);
    FrameReader reader(stream);
    ASSERT_TRUE(reader.next().has_value());
    EXPECT_FALSE(reader.next().has_value());
}

TEST(Reader, ThrowsOnTruncatedPrefix) {
    const auto full = frame_bytes('S', 12);
    const std::span<const std::byte> stream(full.data(), full.size() - 13); // 1 byte left
    FrameReader reader(stream);
    EXPECT_THROW((void)reader.next(), FramingError);
}

TEST(Reader, ThrowsOnLengthUnderHeader) {
    const auto stream = frame_bytes('S', 5);
    FrameReader reader(stream);
    EXPECT_THROW((void)reader.next(), FramingError);
}

TEST(Reader, ThrowsOnTruncatedBody) {
    const auto full = frame_bytes('S', 12);
    const std::span<const std::byte> stream(full.data(), full.size() - 1);
    FrameReader reader(stream);
    EXPECT_THROW((void)reader.next(), FramingError);
}

TEST(Reader, ThrowsOnSpecLengthMismatch) {
    const auto stream = frame_bytes('D', 25); // spec says D is 19
    FrameReader reader(stream);
    EXPECT_THROW((void)reader.next(), FramingError);
}

TEST(Reader, AcceptsUnknownTypeWithAnyLength) {
    // 'Z' is not in the spec; the length prefix alone bounds it.
    auto stream = frame_bytes('Z', 33);
    const auto second = frame_bytes('S', 12);
    stream.insert(stream.end(), second.begin(), second.end());
    FrameReader reader(stream);
    const auto first = reader.next();
    ASSERT_TRUE(first.has_value());
    // has_value asserted above; the checker cannot see through ASSERT_TRUE
    const Frame& first_frame = *first; // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(first_frame.type(), 'Z');
    EXPECT_EQ(classify(first_frame.type()), MessageClass::unknown);
    const auto next = reader.next();
    ASSERT_TRUE(next.has_value());
    const Frame& next_frame = *next; // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ(next_frame.type(), 'S');
    EXPECT_FALSE(reader.next().has_value());
}

} // namespace
