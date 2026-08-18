// ob_fuzz_seed: cut a fuzz corpus out of a framed ITCH stream.
//
// A fuzzer that starts from random bytes spends its budget rediscovering the
// length prefix. Seeding it with real frames starts it past that: one file per
// message type present in the stream, plus a few short multi-frame windows so
// the fuzzer has examples of frames following one another.
//
// The corpus is generated rather than committed. It is derived data — the
// fixture already carries the bytes, and a committed corpus would be a second
// copy free to drift from it.

#include "itch/reader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kWindowFrames = 8; // frames per multi-frame seed
constexpr std::size_t kWindowCount = 16; // how many windows to spread over the stream

bool write_seed(const fs::path& path, std::span<const std::byte> bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "cannot write " << path << "\n";
        return false;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): byte -> char for ostream
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

std::string type_name(unsigned char type) {
    // Printable types name themselves; anything else falls back to its number
    // so the filename stays usable.
    if (type > ' ' && type < 0x7F) {
        return {1, static_cast<char>(type)};
    }
    return "x" + std::to_string(static_cast<unsigned>(type));
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 3) {
        std::cerr << "usage: ob_fuzz_seed FIXTURE OUTDIR\n";
        return 2;
    }

    const fs::path fixture{args[1]};
    const fs::path outdir{args[2]};

    std::error_code ec;
    fs::create_directories(outdir, ec);
    if (ec) {
        std::cerr << "cannot create " << outdir << ": " << ec.message() << "\n";
        return 1;
    }

    const ob::itch::MappedFile file(fixture.string());
    const std::span<const std::byte> stream = file.bytes();

    // Offsets of every frame start, and the first frame seen of each type.
    std::vector<std::size_t> starts;
    std::array<bool, 256> seen{};
    uint64_t written = 0;

    ob::itch::FrameReader reader(stream);
    while (const auto frame = reader.next()) {
        const auto start = static_cast<std::size_t>(frame->offset);
        const std::size_t len = 2 + frame->body.size();
        starts.push_back(start);

        const unsigned char type = frame->type();
        if (!seen[type]) {
            seen[type] = true;
            if (!write_seed(outdir / ("type-" + type_name(type) + ".itch"),
                            stream.subspan(start, len))) {
                return 1;
            }
            ++written;
        }
    }

    if (starts.empty()) {
        std::cerr << "no frames in " << fixture << "\n";
        return 1;
    }

    // Windows spread evenly over the stream, each a run of consecutive frames.
    for (std::size_t i = 0; i < kWindowCount; ++i) {
        const std::size_t first = starts.size() * i / kWindowCount;
        const std::size_t last = std::min(first + kWindowFrames, starts.size());
        const std::size_t begin = starts[first];
        const std::size_t end = last < starts.size() ? starts[last] : stream.size();
        if (!write_seed(outdir / ("window-" + std::to_string(i) + ".itch"),
                        stream.subspan(begin, end - begin))) {
            return 1;
        }
        ++written;
    }

    std::cout << "wrote " << written << " seed(s) to " << outdir << "\n";
    return 0;
}
