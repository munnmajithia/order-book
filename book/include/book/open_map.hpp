#pragma once

// Reserve-sized open-addressing hash map, uint64 key to pointer value.
// Linear probing, tombstone deletion, doubles when live+dead slots pass the
// load limit. Key 0 is reserved as the empty marker, so callers must never
// insert it; ITCH refs and packed level keys are both nonzero by construction.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ob::book {

template <typename V> class OpenMap {
  public:
    explicit OpenMap(std::size_t reserve = 1U << 16U) {
        std::size_t cap = 64;
        while (cap * 10 < reserve * 16) { // sized so reserve fits under load limit
            cap <<= 1U;
        }
        slots_.resize(cap);
    }

    void insert(uint64_t key, V* value) {
        if ((live_ + dead_ + 1) * 10 > slots_.size() * 7) {
            rehash();
        }
        Slot* target = nullptr;
        for (std::size_t i = index_of(key);; i = (i + 1) & (slots_.size() - 1)) {
            Slot& slot = slots_[i];
            if (slot.key == key && slot.value != nullptr) {
                throw std::logic_error("open map: duplicate key");
            }
            if (slot.key == 0) {
                if (target == nullptr) {
                    target = &slot;
                }
                break;
            }
            if (slot.value == nullptr && target == nullptr) {
                target = &slot; // first tombstone on the probe path
            }
        }
        if (target->key != 0) {
            --dead_; // reusing a tombstone
        }
        target->key = key;
        target->value = value;
        ++live_;
    }

    [[nodiscard]] V* find(uint64_t key) const {
        for (std::size_t i = index_of(key);; i = (i + 1) & (slots_.size() - 1)) {
            const Slot& slot = slots_[i];
            if (slot.key == key && slot.value != nullptr) {
                return slot.value;
            }
            if (slot.key == 0) {
                return nullptr;
            }
        }
    }

    void erase(uint64_t key) {
        for (std::size_t i = index_of(key);; i = (i + 1) & (slots_.size() - 1)) {
            Slot& slot = slots_[i];
            if (slot.key == key && slot.value != nullptr) {
                slot.value = nullptr; // tombstone: key kept so probes continue
                --live_;
                ++dead_;
                return;
            }
            if (slot.key == 0) {
                return;
            }
        }
    }

    [[nodiscard]] std::size_t size() const { return live_; }

    template <typename Fn> void for_each(Fn&& fn) const {
        for (const Slot& slot : slots_) {
            if (slot.key != 0 && slot.value != nullptr) {
                fn(slot.key, slot.value);
            }
        }
    }

  private:
    struct Slot {
        uint64_t key = 0;
        V* value = nullptr;
    };

    [[nodiscard]] std::size_t index_of(uint64_t key) const {
        // splitmix64 finalizer
        uint64_t h = key;
        h ^= h >> 30U;
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= h >> 27U;
        h *= 0x94D049BB133111EBULL;
        h ^= h >> 31U;
        return static_cast<std::size_t>(h) & (slots_.size() - 1);
    }

    void rehash() {
        std::vector<Slot> old = std::move(slots_);
        slots_.assign(old.size() * 2, Slot{});
        live_ = 0;
        dead_ = 0;
        for (const Slot& slot : old) {
            if (slot.key != 0 && slot.value != nullptr) {
                insert(slot.key, slot.value);
            }
        }
    }

    std::vector<Slot> slots_;
    std::size_t live_ = 0;
    std::size_t dead_ = 0;
};

} // namespace ob::book
