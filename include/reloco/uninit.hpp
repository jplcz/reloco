// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once
#include <cstddef>
#include <new>
#include <reloco/lifetime.hpp>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace reloco {

/*
 * @class uninit
 * @brief Safe wrapper for uninitialized memory with compile-time state tracking.
 *
 * @details
 * Uses Clang typestates to mathematically prevent reading from uninitialized
 * memory, as well as preventing accidental double-initialization.
 *
 * @code
 * reloco::uninit<TelemetryPacket> pkt;
 * // pkt.get(); // COMPILER ERROR: Called while in 'consumed' (empty) state!
 *
 * pkt.write(0x01, 100); // Transitions to 'unconsumed' (filled) state
 * // pkt.write(0x02, 200); // COMPILER ERROR: Cannot overwrite without destroying!
 *
 * transmit(pkt.get()); // ✅ SUCCESS: Safe to read.
 * @endcode
 */
template <typename T> class RELOCO_OWNER RELOCO_CONSUMABLE(consumed) uninit {
public:
  /**
   * @brief Allocates the memory but leaves it completely uninitialized.
   * @note Starts in the 'consumed' (empty/invalid) state.
   */
  // ReSharper disable once CppPossiblyUninitializedMember
  constexpr uninit() noexcept // NOLINT(*-pro-type-member-init)
      RELOCO_RETURN_TYPESTATE(consumed) {}

  uninit(const uninit &) = delete;
  uninit &operator=(const uninit &) = delete;

  /**
   * @brief Constructs the object in-place.
   * @pre The memory must be uninitialized ('consumed').
   * @post The memory is now initialized ('unconsumed').
   */
  template <typename... Args>
  RELOCO_UNSAFE_BUFFER_USAGE [[nodiscard]] T &write(Args &&...args) noexcept RELOCO_LIFETIMEBOUND
      RELOCO_CALLABLE_WHEN(consumed) RELOCO_SET_TYPESTATE(unconsumed) {
    return *(new (&m_storage) T(std::forward<Args>(args)...));
  }

  /**
   * @brief Escape hatch: Tells the compiler the memory was filled externally (e.g., via DMA).
   * @pre The memory must be uninitialized ('consumed').
   * @post The memory is now considered initialized ('unconsumed').
   */
  RELOCO_UNSAFE_BUFFER_USAGE [[nodiscard]] T *assume_init() noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(consumed)
      RELOCO_SET_TYPESTATE(unconsumed) {
    return std::launder(reinterpret_cast<T *>(&m_storage));
  }

  /**
   * @brief Destroys the initialized object and returns the memory to the uninitialized state.
   * @pre The memory must be initialized ('unconsumed').
   * @post The memory is uninitialized ('consumed').
   */
  RELOCO_UNSAFE_BUFFER_USAGE void destroy() noexcept RELOCO_CALLABLE_WHEN(unconsumed) RELOCO_SET_TYPESTATE(consumed) {
    std::launder(reinterpret_cast<T *>(&m_storage))->~T();
  }

  /**
   * @brief Safe read-only access to the initialized memory.
   * @pre The memory MUST be initialized ('unconsumed').
   */
  RELOCO_UNSAFE_BUFFER_USAGE [[nodiscard]] const T &get() const noexcept RELOCO_LIFETIMEBOUND
      RELOCO_CALLABLE_WHEN(unconsumed) {
    return *std::launder(reinterpret_cast<const T *>(&m_storage));
  }

  /**
   * @brief Safe mutable access to the initialized memory.
   * @pre The memory MUST be initialized ('unconsumed').
   */
  RELOCO_UNSAFE_BUFFER_USAGE [[nodiscard]] T &get_mut() noexcept RELOCO_LIFETIMEBOUND RELOCO_CALLABLE_WHEN(unconsumed) {
    return *std::launder(reinterpret_cast<T *>(&m_storage));
  }

  /**
   * @brief Extracts the value via move-semantics and resets the state to uninitialized.
   * @pre The memory MUST be initialized ('unconsumed').
   * @post The memory is returned to the uninitialized ('consumed') state.
   */
  RELOCO_UNSAFE_BUFFER_USAGE [[nodiscard]] T extract() noexcept RELOCO_CALLABLE_WHEN(unconsumed)
      RELOCO_SET_TYPESTATE(consumed) {
    T val = std::move(get_mut());
    destroy(); // Optional depending on if T is trivially destructible, but good practice.
    return val;
  }

private:
  // Guarantees exact size and alignment, but leaves it uninitialized
  alignas(T) std::byte m_storage[sizeof(T)];
};

} // namespace reloco

RELOCO_END_UNSAFE_BUFFER_USAGE
