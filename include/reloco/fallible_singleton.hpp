#pragma once
#include <atomic>
#include <reloco/concepts.hpp>
#include <reloco/fallible_constructed.hpp>

namespace reloco {

/**
 * Singleton patter, but it's not thread safe, as
 * ``static_fallible_constructed::try_init()`` is not thread-safe.
 * Primary usage of this class is controlled static initialization
 * using single thread, such as main program initialization,
 * to avoid static initialization order fiasco.
 */
template <is_fallible_initializable T> class fallible_singleton {
public:
  static result<T *> instance() noexcept {
    if (!m_internal_storage) {
      auto res = m_internal_storage.try_init();
      if (!res)
        return std::unexpected(res.error());
    }
    return m_internal_storage.get();
  }

private:
  static inline static_fallible_constructed<T> m_internal_storage;
};

template <typename T>
concept singleton_lock_traits = requires(typename T::lock_type &l) {
  typename T::lock_type;
  { T::lock(l) } -> std::same_as<void>;
  { T::unlock(l) } -> std::same_as<void>;
};

template <is_fallible_initializable T, singleton_lock_traits Traits>
class atomic_fallible_singleton {
  static inline std::atomic<int> m_state{0};
  static inline static_fallible_constructed<T> m_storage;

  static constexpr int UNINITIALIZED = 0;
  static constexpr int READY = 1;

public:
  static result<T *>
  instance(typename Traits::lock_type &external_lock) noexcept {
    // Acquire-load ensures we see the initialized memory if ready
    if (m_state.load(std::memory_order_acquire) == READY) [[likely]] {
      return m_storage.get();
    }

    Traits::lock(external_lock);

    result<T *> res;
    if (m_state.load(std::memory_order_relaxed) == UNINITIALIZED) {
      auto init_res = m_storage.try_init();
      if (!init_res) {
        res = std::unexpected(init_res.error());
      } else {
        m_state.store(READY, std::memory_order_release);
        res = m_storage.get();
      }
    } else {
      res = m_storage.get();
    }

    Traits::unlock(external_lock);
    return res;
  }
};

} // namespace reloco
