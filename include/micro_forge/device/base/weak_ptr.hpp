/**
 * @file weak_ptr.hpp
 * @brief Single-threaded lifetime observers for migrated MCU components.
 * @version 0.1.0
 */
#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <memory>
#include <utility>

namespace micro_forge::device {

template <typename T>
class WeakPtrFactory;

/** @brief An observer whose token is invalidated before the owning component dies. */
template <typename T>
class WeakPtr {
public:
    WeakPtr() = default;
    /// @brief An empty observer can be passed wherever a nullable observer is accepted.
    explicit(false) WeakPtr(std::nullptr_t) noexcept {}

    /**
     * @brief Converts an observer without changing ownership or extending the owner lifetime.
     * @param[in] other Observer of a pointer-convertible component type.
     */
    template <typename U>
        requires std::convertible_to<U*, T*>
    explicit(false) WeakPtr(const WeakPtr<U>& other) noexcept
        : ptr_(other.ptr_), alive_(other.alive_) {}

    /// @brief True only while the observed component and its token are live.
    [[nodiscard]] bool is_valid() const noexcept { return ptr_ != nullptr && alive_ && *alive_; }
    /// @brief Returns the live component, or null after destruction/invalidation.
    [[nodiscard]] T* get() const noexcept { return is_valid() ? ptr_ : nullptr; }
    /// @brief Tests validity without transferring ownership.
    explicit operator bool() const noexcept { return is_valid(); }
    /// @brief Accesses a component whose validity the caller has checked.
    [[nodiscard]] T* operator->() const noexcept {
        assert(is_valid());
        return ptr_;
    }
    /// @brief Dereferences a component whose validity the caller has checked.
    [[nodiscard]] T& operator*() const noexcept {
        assert(is_valid());
        return *ptr_;
    }
    /// @brief Drops this observation without affecting other observers.
    void reset() noexcept {
        ptr_ = nullptr;
        alive_.reset();
    }

private:
    WeakPtr(T* ptr, std::shared_ptr<bool> alive) : ptr_(ptr), alive_(std::move(alive)) {}
    T* ptr_ = nullptr;
    std::shared_ptr<bool> alive_;
    template <typename U>
    friend class WeakPtr;
    template <typename U>
    friend class WeakPtrFactory;
};

/** @brief Owns the lifetime token; declare this last in a component. */
template <typename T>
class WeakPtrFactory {
public:
    /**
     * @brief Starts a token for a non-null exclusively owned component.
     * @param[in] owner Component which outlives this factory.
     */
    explicit WeakPtrFactory(T* owner) : owner_(owner) { assert(owner_ != nullptr); }
    ~WeakPtrFactory() { *alive_ = false; }
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;
    WeakPtrFactory(WeakPtrFactory&&) = delete;
    WeakPtrFactory& operator=(WeakPtrFactory&&) = delete;

    /// @brief Issues an observer sharing the current validity token.
    [[nodiscard]] WeakPtr<T> get_weak_ptr() const { return WeakPtr<T>(owner_, alive_); }
    /// @brief Invalidates existing observers while permitting new observations.
    void invalidate_weak_ptrs() {
        auto replacement = std::make_shared<bool>(true);
        *alive_ = false;
        alive_ = std::move(replacement);
    }
    /// @brief True when the current token has external observers.
    [[nodiscard]] bool has_weak_ptrs() const noexcept { return alive_.use_count() > 1; }

private:
    T* owner_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace micro_forge::device
