// Standalone driver for the fuzz entry point.
//
// libFuzzer is a clang-only runtime, but the entry point it drives is ordinary
// code that every preset should compile, lint and sanitize. This driver links
// the same function and replays whatever files (or directories of files) it is
// given, so the fuzz path runs under all ten presets in CI and a crash artifact
// can be reproduced from any build, not only a fuzzer one.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size);

namespace {

namespace fs = std::filesystem;

bool replay_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "cannot open " << path << "\n";
        return false;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() < 2) {
        std::cerr << "usage: ob_fuzz_replay FILE_OR_DIR...\n";
        return 2;
    }

    uint64_t replayed = 0;
    for (const std::string_view arg : args.subspan(1)) {
        const fs::path path{arg};
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            for (const auto& entry : fs::recursive_directory_iterator(path)) {
                if (entry.is_regular_file() && !replay_file(entry.path())) {
                    return 1;
                }
                replayed += entry.is_regular_file() ? 1 : 0;
            }
        } else {
            if (!replay_file(path)) {
                return 1;
            }
            ++replayed;
        }
    }

    std::cout << "replayed " << replayed << " input(s)\n";
    return 0;
}
