#pragma once

// mmap'd reader over the length-prefixed ITCH framing.
//
// The reader validates framing itself rather than trusting the decoder to
// notice: a truncated prefix, a length under the protocol header size, a
// truncated body, or a known-type length that disagrees with the spec all
// throw with the file offset attached. This mirrors tools/slice exactly, so
// the fixture producer and consumer agree on what a well-framed stream is.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace ob::itch {

class MappedFile {
  public:
    explicit MappedFile(const std::string& path);
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&&) = delete;
    MappedFile& operator=(MappedFile&&) = delete;

    [[nodiscard]] std::span<const std::byte> bytes() const { return {data_, size_}; }

  private:
    const std::byte* data_ = nullptr;
    std::size_t size_ = 0;
};

struct Frame {
    std::span<const std::byte> body; // the message, length prefix stripped
    uint64_t offset = 0;             // file offset of the length prefix

    [[nodiscard]] unsigned char type() const { return static_cast<unsigned char>(body[0]); }
};

class FramingError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class FrameReader {
  public:
    explicit FrameReader(std::span<const std::byte> stream) : stream_(stream) {}

    // Next frame, or nullopt at a clean end of stream. Throws FramingError
    // when the stream is not well-framed.
    std::optional<Frame> next();

    [[nodiscard]] uint64_t offset() const { return pos_; }

  private:
    std::span<const std::byte> stream_;
    uint64_t pos_ = 0;
};

} // namespace ob::itch
