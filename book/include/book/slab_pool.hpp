#pragma once

// Slab allocator for pool nodes: allocates fixed-size slabs, never frees
// until destruction, recycles nodes through an intrusive free list threaded
// via the node's own next pointer. No per-node allocation after warmup.

#include <concepts>
#include <cstddef>
#include <memory>
#include <vector>

namespace ob::book {

template <typename T>
concept PoolNode = requires(T t) {
    { t.next } -> std::convertible_to<T*>;
};

template <PoolNode T> class SlabPool {
  public:
    static constexpr std::size_t kSlabNodes = 4096;

    T* allocate() {
        if (free_ == nullptr) {
            grow();
        }
        T* node = free_;
        free_ = node->next;
        return node;
    }

    void release(T* node) {
        node->next = free_;
        free_ = node;
    }

  private:
    void grow() {
        slabs_.push_back(std::make_unique<T[]>(kSlabNodes));
        T* slab = slabs_.back().get();
        for (std::size_t i = 0; i < kSlabNodes; ++i) {
            slab[i].next = i + 1 < kSlabNodes ? &slab[i + 1] : nullptr;
        }
        free_ = slab;
    }

    std::vector<std::unique_ptr<T[]>> slabs_;
    T* free_ = nullptr;
};

} // namespace ob::book
