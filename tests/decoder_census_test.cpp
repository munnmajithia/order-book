// Decode dispatch over the whole fixture, census asserted exactly against
// the counts the slice tool committed alongside the fixture. The producer
// (tools/slice) and consumer (this parser) have independent length tables
// and keep rules, so agreement here is a real cross-check, not bookkeeping.
#include "itch/decoder.hpp"
#include "itch/messages.hpp"
#include "itch/reader.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ob::itch;

struct CountingHandler {
    std::map<char, uint64_t> counts;
    uint64_t skipped = 0;
    uint64_t unknown = 0;
    uint64_t bytes = 0;

    template <typename Msg> void operator()(const Msg& msg) {
        ++counts[msg.header.type];
        bytes += 2 + sizeof(Msg);
    }
    void operator()(const AddOrderMpid& msg) {
        ++counts[msg.add.header.type];
        bytes += 2 + sizeof(AddOrderMpid);
    }
    void operator()(const OrderExecutedPrice& msg) {
        ++counts[msg.executed.header.type];
        bytes += 2 + sizeof(OrderExecutedPrice);
    }
    void on_skipped(const Frame& frame) {
        ++counts[static_cast<char>(frame.type())];
        ++skipped;
        bytes += 2 + frame.body.size();
    }
    void on_unknown(const Frame& frame) {
        ++counts[static_cast<char>(frame.type())];
        ++unknown;
        bytes += 2 + frame.body.size();
    }
};

std::map<char, uint64_t> read_committed_census(uint64_t& frames, uint64_t& bytes) {
    std::ifstream in(std::string(OB_TEST_DATA_DIR) + "/fixture-census.txt");
    std::map<char, uint64_t> counts;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string key;
        uint64_t value = 0;
        fields >> key >> value;
        if (key == "frames") {
            frames = value;
        } else if (key == "bytes") {
            bytes = value;
        } else {
            EXPECT_EQ(key.size(), 1U) << "bad census line: " << line;
            counts[key.front()] = value;
        }
    }
    return counts;
}

TEST(DecoderCensus, MatchesCommittedCountsExactly) {
    uint64_t committed_frames = 0;
    uint64_t committed_bytes = 0;
    const auto committed = read_committed_census(committed_frames, committed_bytes);
    ASSERT_FALSE(committed.empty());

    const MappedFile file(std::string(OB_TEST_DATA_DIR) + "/fixture.itch");
    FrameReader reader(file.bytes());
    CountingHandler handler;
    uint64_t frames = 0;
    while (const auto frame = reader.next()) {
        decode(*frame, handler);
        ++frames;
    }

    EXPECT_EQ(handler.counts, committed); // exact, type by type
    EXPECT_EQ(frames, committed_frames);
    EXPECT_EQ(handler.bytes, committed_bytes);
    EXPECT_EQ(handler.unknown, 0U);
    EXPECT_EQ(handler.skipped, 529U);
}

TEST(Decoder, UnknownTypeTakesUnknownPath) {
    // The fixture contains no undefined types on purpose; synthesize one.
    std::vector<std::byte> body(20, std::byte{0});
    body[0] = std::byte{'Z'};
    const Frame frame{.body = std::span<const std::byte>(body), .offset = 0};
    CountingHandler handler;
    decode(frame, handler);
    EXPECT_EQ(handler.unknown, 1U);
    EXPECT_EQ(handler.skipped, 0U);
    EXPECT_EQ(handler.counts.at('Z'), 1U);
}

TEST(Decoder, DispatchesEachDecodedType) {
    CountingHandler handler;
    for (const auto& [type, size] : std::vector<std::pair<char, std::size_t>>{{'S', 12},
                                                                              {'R', 39},
                                                                              {'A', 36},
                                                                              {'F', 40},
                                                                              {'E', 31},
                                                                              {'C', 36},
                                                                              {'X', 23},
                                                                              {'D', 19},
                                                                              {'U', 35},
                                                                              {'P', 44}}) {
        std::vector<std::byte> body(size, std::byte{0});
        body[0] = std::byte{static_cast<unsigned char>(type)};
        const Frame frame{.body = std::span<const std::byte>(body), .offset = 0};
        decode(frame, handler);
        EXPECT_EQ(handler.counts.at(type), 1U) << type;
    }
    EXPECT_EQ(handler.skipped, 0U);
    EXPECT_EQ(handler.unknown, 0U);
}

} // namespace
