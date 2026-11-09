/// @file test_cache.cpp
/// @brief The generic cache: hits, misses, LRU replacement, invalidation.

#include <gtest/gtest.h>

#include <micro_forge/base/cache.hpp>

namespace {

using micro_forge::base::SetAssocCache;

TEST(CacheTest, DirectMappedHitAndMiss) {
    SetAssocCache<std::uint64_t, int, 16> cache(8);
    cache.put(1, 100);
    const auto hit = cache.get(1);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(100, *hit);
    EXPECT_FALSE(cache.get(2).has_value());
    EXPECT_EQ(1U, cache.stats().hits);
    EXPECT_EQ(1U, cache.stats().misses);
}

TEST(CacheTest, LruReplacesOldestWay) {
    // 4 entries, 2 ways: sets of 2. Keys 0,1,2,3 map to sets 0,1,0,1.
    // NOLINTNEXTLINE(misc-const-correctness) -- get()/put() mutate the cache
    SetAssocCache<std::uint64_t, int, 16, 2> cache(4);
    cache.put(0, 10);
    cache.put(2, 20); // set 0 (keys 0,2,4 all hash to set 0) now full
    cache.put(4, 40); // evicts the least recently used entry of the set
    EXPECT_FALSE(cache.get(0).has_value());
    const auto recent = cache.get(2);
    ASSERT_TRUE(recent.has_value());
    EXPECT_EQ(20, *recent);
    const auto inserted = cache.get(4);
    ASSERT_TRUE(inserted.has_value());
    EXPECT_EQ(40, *inserted);
}

TEST(CacheTest, EraseAndClear) {
    SetAssocCache<std::uint64_t, int, 16> cache(8);
    cache.put(7, 70);
    cache.erase(7);
    EXPECT_FALSE(cache.get(7).has_value());
    cache.put(3, 30);
    cache.clear();
    EXPECT_FALSE(cache.get(3).has_value());
    // Statistics survive clear().
    EXPECT_GT(cache.stats().misses, 0U);
}

TEST(CacheTest, ZeroCapacityIsDisabled) {
    // get/put() update the hit counters, so this binding is not const.
    // NOLINTNEXTLINE(misc-const-correctness)
    SetAssocCache<std::uint64_t, int, 16> cache(0);
    cache.put(1, 1);
    EXPECT_FALSE(cache.get(1).has_value());
    EXPECT_EQ(0U, cache.stats().hits);
}

TEST(CacheTest, WaysRoundDownToMultiple) {
    const SetAssocCache<std::uint64_t, int, 16, 4> cache(10);
    EXPECT_EQ(8U, cache.capacity()); // 10 rounded down to 8
    EXPECT_EQ(4U, cache.ways());
}

} // namespace
