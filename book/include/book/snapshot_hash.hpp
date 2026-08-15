#pragma once

// FNV-1a over snapshot text, shared by both books so their golden files
// carry the same fingerprint format.

#include <cstdint>
#include <string>

namespace ob::book {

[[nodiscard]] inline uint64_t fnv1a(const std::string& text) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace ob::book
