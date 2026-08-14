#include "itch/reader.hpp"

#include "itch/messages.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ob::itch {

MappedFile::MappedFile(const std::string& path) {
    const int fd =
        ::open(path.c_str(), O_RDONLY | O_CLOEXEC); // NOLINT(cppcoreguidelines-pro-type-vararg)
    if (fd < 0) {
        throw FramingError("cannot open " + path);
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size < 0) {
        ::close(fd);
        throw FramingError("cannot stat " + path);
    }
    size_ = static_cast<std::size_t>(st.st_size);
    if (size_ > 0) {
        void* map = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map ==
            MAP_FAILED) { // NOLINT(performance-no-int-to-ptr,cppcoreguidelines-pro-type-cstyle-cast)
            ::close(fd);
            throw FramingError("cannot mmap " + path);
        }
        data_ = static_cast<const std::byte*>(map);
    }
    ::close(fd);
}

MappedFile::~MappedFile() {
    if (data_ != nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): munmap takes void*
        ::munmap(const_cast<std::byte*>(data_), size_);
    }
}

std::optional<Frame> FrameReader::next() {
    const std::size_t remaining = stream_.size() - static_cast<std::size_t>(pos_);
    if (remaining == 0) {
        return std::nullopt;
    }
    if (remaining < 2) {
        throw FramingError("truncated length prefix at offset " + std::to_string(pos_));
    }
    const auto hi = static_cast<uint16_t>(stream_[static_cast<std::size_t>(pos_)]);
    const auto lo = static_cast<uint16_t>(stream_[static_cast<std::size_t>(pos_) + 1]);
    const auto len = static_cast<uint16_t>(static_cast<uint16_t>(hi << 8) | lo);
    if (len < sizeof(Header) + 1) {
        throw FramingError("length " + std::to_string(len) + " under header size at offset " +
                           std::to_string(pos_));
    }
    if (remaining < 2 + static_cast<std::size_t>(len)) {
        throw FramingError("truncated message at offset " + std::to_string(pos_));
    }
    Frame frame{.body = stream_.subspan(static_cast<std::size_t>(pos_) + 2, len), .offset = pos_};
    const uint16_t expected = spec_length(frame.type());
    if (expected != 0 && len != expected) {
        throw FramingError(std::string("type ") + static_cast<char>(frame.type()) + " length " +
                           std::to_string(len) + " != spec " + std::to_string(expected) +
                           " at offset " + std::to_string(pos_));
    }
    pos_ += 2 + static_cast<uint64_t>(len);
    return frame;
}

} // namespace ob::itch
