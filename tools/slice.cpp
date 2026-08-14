// ob_slice: cut a deterministic CI fixture from a full-day ITCH 5.0 stream.
//
// Reads the uncompressed length-prefixed stream on stdin and keeps: every
// system event (S), the directory entry (R) for each requested symbol, and
// every later message whose stock locate belongs to a requested symbol,
// unknown types included; the length prefix bounds every message and the
// 11-byte header is protocol-wide, so unknown types carry no framing risk.
// Stops before the first kept message that would push the output past the
// byte ceiling. Emits the fixture and a census of what it kept.
//
// The message-length table is duplicated from the spec on purpose: coupling
// the tool that generates test data to the parser under test would let a
// parser bug silently reshape the fixture.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ITCH 5.0 total message lengths (type byte included), 0 = not in the spec.
constexpr std::array<uint16_t, 256> kSpecLength = [] {
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

constexpr std::size_t kHeaderLen = 11; // type + locate + tracking + timestamp

uint16_t be16(unsigned char hi, unsigned char lo) {
    return static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
}

std::string pad_symbol(std::string s) {
    s.resize(8, ' ');
    return s;
}

void split_symbols(const std::string& list, std::vector<std::string>& out) {
    std::size_t pos = 0;
    while (pos != std::string::npos) {
        const std::size_t comma = list.find(',', pos);
        const std::string sym =
            list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (!sym.empty()) {
            out.push_back(pad_symbol(sym));
        }
        pos = comma == std::string::npos ? std::string::npos : comma + 1;
    }
}

struct Options {
    std::vector<std::string> symbols;
    uint64_t max_bytes = 0;
    std::string out_path;
    std::string census_path;
};

bool parse_args(std::span<char*> args, Options& opt) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--symbols" && i + 1 < args.size()) {
            split_symbols(args[++i], opt.symbols);
        } else if (arg == "--max-bytes" && i + 1 < args.size()) {
            opt.max_bytes = std::stoull(args[++i]);
        } else if (arg == "--out" && i + 1 < args.size()) {
            opt.out_path = args[++i];
        } else if (arg == "--census" && i + 1 < args.size()) {
            opt.census_path = args[++i];
        } else {
            return false;
        }
    }
    return !opt.symbols.empty() && opt.max_bytes > 0 && !opt.out_path.empty() &&
           !opt.census_path.empty();
}

// Keep decision; records the locate of a matching directory entry.
bool should_keep(unsigned char type, uint16_t locate, std::span<const unsigned char> body,
                 const std::vector<std::string>& symbols, std::set<uint16_t>& locates) {
    if (type == 'S') {
        return true;
    }
    if (type == 'R') {
        // Viewing raw bytes as chars for comparison; no object is reinterpreted.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const std::string_view stock(reinterpret_cast<const char*>(body.data() + kHeaderLen), 8);
        for (const auto& sym : symbols) {
            if (stock == sym) {
                locates.insert(locate);
                return true;
            }
        }
        return false;
    }
    return locates.contains(locate);
}

bool write_census(const std::string& path, const std::array<uint64_t, 256>& census,
                  uint64_t frames_kept, uint64_t bytes_kept) {
    std::ofstream out(path);
    out << "# kept message count per type\n";
    for (std::size_t t = 0; t < census.size(); ++t) {
        if (census[t] != 0) {
            out << static_cast<char>(t) << " " << census[t] << "\n";
        }
    }
    out << "frames " << frames_kept << "\n";
    out << "bytes " << bytes_kept << "\n";
    return static_cast<bool>(out);
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(std::span<char*>(argv, static_cast<std::size_t>(argc)), opt)) {
        std::cerr << "usage: ob_slice --symbols A,B,C --max-bytes N --out FILE --census FILE\n"
                  << "reads the uncompressed ITCH stream on stdin\n";
        return 2;
    }

    std::ofstream out(opt.out_path, std::ios::binary);
    if (!out) {
        std::cerr << "cannot open " << opt.out_path << "\n";
        return 1;
    }

    std::set<uint16_t> locates;
    std::array<uint64_t, 256> census{};
    uint64_t frames_in = 0;
    uint64_t frames_kept = 0;
    uint64_t bytes_kept = 0;
    uint64_t unknown_kept = 0;
    bool ceiling_hit = false;

    std::array<unsigned char, 2 + 65535> buf{};
    while (true) {
        const std::size_t got = std::fread(buf.data(), 1, 2, stdin);
        if (got == 0) {
            break; // clean end of stream
        }
        if (got != 2) {
            std::cerr << "truncated length prefix at frame " << frames_in << "\n";
            return 1;
        }
        const uint16_t len = be16(buf[0], buf[1]);
        if (len < kHeaderLen + 1) {
            std::cerr << "frame " << frames_in << ": length " << len << " under header size\n";
            return 1;
        }
        if (std::fread(std::next(buf.data(), 2), 1, len, stdin) != len) {
            std::cerr << "truncated message at frame " << frames_in << "\n";
            return 1;
        }
        ++frames_in;

        const unsigned char type = buf[2];
        const uint16_t spec_len = kSpecLength[type];
        if (spec_len != 0 && len != spec_len) {
            std::cerr << "frame " << frames_in << ": type " << static_cast<char>(type) << " length "
                      << len << " != spec " << spec_len << "\n";
            return 1;
        }
        const uint16_t locate = be16(buf[3], buf[4]);

        if (!should_keep(type, locate, std::span<const unsigned char>(buf).subspan(2, len),
                         opt.symbols, locates)) {
            continue;
        }

        if (bytes_kept + 2 + len > opt.max_bytes) {
            ceiling_hit = true;
            break;
        }
        // ofstream takes char*; these are the same raw bytes.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        out.write(reinterpret_cast<const char*>(buf.data()), 2 + len);
        ++census[type];
        ++frames_kept;
        bytes_kept += 2 + static_cast<uint64_t>(len);
        if (spec_len == 0) {
            ++unknown_kept;
        }
    }
    out.close();
    if (!out) {
        std::cerr << "write failure on " << opt.out_path << "\n";
        return 1;
    }

    if (!write_census(opt.census_path, census, frames_kept, bytes_kept)) {
        std::cerr << "write failure on " << opt.census_path << "\n";
        return 1;
    }

    std::cerr << "read " << frames_in << " frames, kept " << frames_kept << " (" << bytes_kept
              << " bytes, " << unknown_kept << " of unknown type"
              << (ceiling_hit ? ", stopped at byte ceiling" : ", reached end of stream") << ")\n";
    return 0;
}
