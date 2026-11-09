/**
 * @file cache.hpp
 * @brief Generic set-associative cache over fixed-capacity storage.
 * @version 0.1.0
 *
 * Storage is a std::array bounded at compile time: no heap, no
 * constructor failure, no copies that allocate. The active capacity can
 * be lowered at construction but never exceeds the compile-time bound,
 * so embedded builds (no heap) can use the same component as the native
 * and wasm engines.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace micro_forge::base {

/**
 * @brief A set-associative LRU cache with fixed-capacity storage.
 *
 * @tparam Key The lookup key type.
 * @tparam Value The cached value type.
 * @tparam Capacity Compile-time entry bound (must be >= Ways).
 * @tparam Ways Ways per set (1 = direct-mapped).
 * @tparam Hash Key hash (defaults to std::hash).
 */
template <typename Key, typename Value, std::size_t Capacity, std::size_t Ways = 1,
          typename Hash = std::hash<Key>>
class SetAssocCache {
    static_assert(Ways >= 1, "a cache needs at least one way");
    static_assert(Capacity >= Ways, "capacity must cover at least one set");

public:
    /// @brief Hit/miss counters since construction.
    struct Stats {
        std::uint64_t hits = 0;   ///< Lookups served from the cache.
        std::uint64_t misses = 0; ///< Lookups that had to fall through.
    };

    /**
     * @brief Builds a cache with up to @p capacity active entries.
     *
     * Associativity is the template's Ways (compile-time); only the
     * active entry count is adjustable at runtime.
     *
     * @param[in] capacity Active entries; clamped to Capacity and rounded
     *            down to a multiple of Ways. Zero disables the cache.
     */
    explicit SetAssocCache(std::size_t capacity = Capacity) {
        const std::size_t want = capacity > Capacity ? Capacity : capacity;
        const std::size_t entries = (want / Ways) * Ways;
        sets_ = entries / Ways;
        active_ = entries;
    }

    /**
     * @brief Looks up @p key.
     *
     * @param[in] key The key to find.
     * @return The value on a hit (and marks it recently used).
     */
    [[nodiscard]] std::optional<Value> get(const Key& key) {
        const Value* found = find(key);
        if (found == nullptr) {
            return std::nullopt;
        }
        return *found;
    }

    /**
     * @brief Looks up @p key without copying the value.
     *
     * The returned pointer stays valid through the next put()/erase()/
     * clear() on this cache; the hot path uses it to avoid copying large
     * values every instruction.
     *
     * @param[in] key The key to find.
     * @return The cached value, or nullptr on a miss.
     */
    [[nodiscard]] const Value* find(const Key& key) {
        if (sets_ == 0) {
            ++stats_.misses;
            return nullptr;
        }
        const std::size_t set = set_of(key);
        for (std::size_t way = 0; way < Ways; ++way) {
            Entry& entry = entries_[(set * Ways) + way];
            if (entry.valid && entry.key == key) {
                entry.stamp = ++clock_;
                ++stats_.hits;
                return &entry.value;
            }
        }
        ++stats_.misses;
        return nullptr;
    }

    /**
     * @brief Inserts or refreshes @p key.
     *
     * @param[in] key The key to store.
     * @param[in] value The value to store.
     */
    void put(const Key& key, const Value& value) {
        if (sets_ == 0) {
            return;
        }
        const std::size_t set = set_of(key);
        Entry* victim = nullptr;
        for (std::size_t way = 0; way < Ways; ++way) {
            Entry& entry = entries_[(set * Ways) + way];
            if (entry.valid && entry.key == key) {
                entry.value = value;
                entry.stamp = ++clock_;
                return;
            }
            if (!entry.valid && victim == nullptr) {
                victim = &entry;
            }
        }
        if (victim == nullptr) {
            victim = &entries_[static_cast<std::size_t>(set) * Ways];
            for (std::size_t way = 1; way < Ways; ++way) {
                Entry& entry = entries_[(set * Ways) + way];
                if (entry.stamp < victim->stamp) {
                    victim = &entry;
                }
            }
        }
        victim->key = key;
        victim->value = value;
        victim->stamp = ++clock_;
        victim->valid = true;
    }

    /**
     * @brief Drops one key.
     *
     * @param[in] key The key to remove.
     */
    void erase(const Key& key) {
        if (sets_ == 0) {
            return;
        }
        const std::size_t set = set_of(key);
        for (std::size_t way = 0; way < Ways; ++way) {
            Entry& entry = entries_[(set * Ways) + way];
            if (entry.valid && entry.key == key) {
                entry.valid = false;
                return;
            }
        }
    }

    /// @brief Drops every entry (keeps the statistics).
    void clear() {
        for (Entry& entry : entries_) {
            entry.valid = false;
        }
    }

    /// @brief Active entries (multiple of @p ways).
    [[nodiscard]] std::size_t capacity() const { return active_; }
    /// @brief Compile-time entry bound.
    [[nodiscard]] static constexpr std::size_t max_capacity() { return Capacity; }
    /// @brief Ways per set (the template's Ways).
    [[nodiscard]] static constexpr std::size_t ways() { return Ways; }
    /// @brief Hit/miss counters.
    [[nodiscard]] Stats stats() const { return stats_; }

private:
    struct Entry {
        Key key{};
        Value value{};
        std::uint64_t stamp = 0;
        bool valid = false;
    };

    [[nodiscard]] std::size_t set_of(const Key& key) const {
        // sets_ is a power of two (Capacity/Ways), so this is one AND.
        return Hash{}(key) & (sets_ - 1U);
    }

    std::array<Entry, Capacity> entries_{};
    std::size_t sets_ = 0;
    std::size_t active_ = 0;
    std::uint64_t clock_ = 0;
    Stats stats_{};
};

} // namespace micro_forge::base
