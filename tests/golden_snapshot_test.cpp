// The golden test: replay the whole CI fixture through the decoder into the
// reference book and require the end-of-fixture snapshot to match the
// committed golden file byte for byte.
//
// Regenerate deliberately with OB_REGEN_SNAPSHOT=1 (writes the committed
// file); the diff is then the review artifact.
#include "book/reference_book.hpp"
#include "itch/decoder.hpp"
#include "itch/reader.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using namespace ob::itch;
using ob::book::ReferenceBook;

struct BookHandler {
    ReferenceBook book;
    template <typename Msg> void operator()(const Msg& msg) { book.apply(msg); }
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): handler interface
    void on_skipped(const Frame& frame) { (void)frame; }
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static): handler interface
    void on_unknown(const Frame& frame) {
        FAIL() << "unknown type in fixture at offset " << frame.offset;
    }
};

TEST(GoldenSnapshot, EndOfFixtureBookMatchesCommitted) {
    const std::string golden_path = std::string(OB_TEST_DATA_DIR) + "/fixture-snapshot.txt";

    const MappedFile file(std::string(OB_TEST_DATA_DIR) + "/fixture.itch");
    FrameReader reader(file.bytes());
    BookHandler handler;
    uint64_t frames = 0;
    while (const auto frame = reader.next()) {
        decode(*frame, handler);
        // Debug-replay invariant sweep; crossing is only enforced inside
        // market hours because the pre-open book crosses legally.
        if (++frames % 4096 == 0) {
            handler.book.check_invariants(handler.book.in_market_hours());
        }
    }
    handler.book.check_invariants(handler.book.in_market_hours());

    std::ostringstream produced;
    handler.book.write_snapshot(produced);

    if (std::getenv("OB_REGEN_SNAPSHOT") != nullptr) {
        std::ofstream out(golden_path);
        out << produced.str();
        ASSERT_TRUE(out) << "failed to write " << golden_path;
        GTEST_SKIP() << "regenerated " << golden_path;
    }

    const std::ifstream in(golden_path);
    ASSERT_TRUE(in) << "missing golden file " << golden_path
                    << " (regenerate with OB_REGEN_SNAPSHOT=1)";
    std::stringstream committed;
    committed << in.rdbuf();
    EXPECT_EQ(produced.str(), committed.str());
}

} // namespace
