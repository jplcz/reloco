#pragma once
#include <reloco/assert.hpp>
#include <reloco/concepts.hpp>
#include <reloco/core.hpp>

namespace reloco {

template <is_fallible_initializable T> class fallible_constructed;
template <is_fallible_initializable T> class fallible_allocated;
template <is_fallible_initializable T> class static_fallible_constructed;

namespace detail {
template <typename T> class constructor_key {
  // Only these two classes can construct a key
  template <is_fallible_initializable U>
  friend class reloco::fallible_constructed;
  template <is_fallible_initializable U>
  friend class reloco::fallible_allocated;
  template <is_fallible_initializable U>
  friend class reloco::static_fallible_constructed;
  constexpr constructor_key() noexcept = default;
};
} // namespace detail

template <is_fallible_initializable T> class fallible_constructed {
public:
  constexpr fallible_constructed() noexcept : m_dummy{}, m_initialized(false) {}

  fallible_constructed(const fallible_constructed &) = delete;
  fallible_constructed &operator=(const fallible_constructed &) = delete;

  ~fallible_constructed() noexcept {
    if (m_initialized) {
      m_storage.~T();
    }
  }

  fallible_constructed(fallible_constructed &&other) noexcept
    requires std::move_constructible<T>
  {
    if (other.m_initialized) {
      // Placement move-construct T into our storage
      new (&m_storage) T(std::move(other.m_storage));
      m_initialized = true;

      // Clean up the source
      other.m_storage.~T();
      other.m_initialized = false;
    } else {
      m_initialized = false;
    }
  }

  fallible_constructed &operator=(fallible_constructed &&other) noexcept
    requires std::move_constructible<T> && std::is_move_assignable_v<T>
  {
    if (this != &other) {
      if (m_initialized) {
        m_storage.~T();
        m_initialized = false;
      }
      if (other.m_initialized) {
        new (&m_storage) T(std::move(other.m_storage));
        m_initialized = true;
        other.m_storage.~T();
        other.m_initialized = false;
      }
    }
    return *this;
  }

  result<void> try_init() noexcept {
    if (m_initialized)
      return {};

    T *obj = new (&m_storage) T(detail::constructor_key<T>{});

    auto res = obj->try_init(detail::constructor_key<T>{});
    if (!res) {
      obj->~T();
      return res;
    }

    m_initialized = true;
    return {};
  }

  [[nodiscard]] T *unsafe_get() noexcept { return &m_storage; }

  [[nodiscard]] const T *unsafe_get() const noexcept { return &m_storage; }

  [[nodiscard]] T *get() noexcept {
    RELOCO_ASSERT(m_initialized,
                  "Accessing fallible_constructed before try_init()");
    return &m_storage;
  }

  [[nodiscard]] const T *get() const noexcept {
    RELOCO_ASSERT(m_initialized,
                  "Accessing fallible_constructed before try_init()");
    return &m_storage;
  }

  [[nodiscard]] T &operator*() noexcept {
    RELOCO_ASSERT(m_initialized,
                  "Dereferencing fallible_constructed before try_init()");
    return m_storage;
  }

  [[nodiscard]] const T &operator*() const noexcept {
    RELOCO_ASSERT(m_initialized,
                  "Dereferencing fallible_constructed before try_init()");
    return m_storage;
  }

  [[nodiscard]] T *operator->() noexcept {
    RELOCO_ASSERT(m_initialized,
                  "Accessing member of fallible_constructed before try_init()");
    return &m_storage;
  }

  [[nodiscard]] const T *operator->() const noexcept {
    RELOCO_ASSERT(m_initialized,
                  "Accessing member of fallible_constructed before try_init()");
    return &m_storage;
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return m_initialized;
  }

  [[nodiscard]] result<T *> try_get() noexcept {
    if (!m_initialized) [[unlikely]] {
      return unexpected(error::not_initialized);
    }
    return &m_storage;
  }

  [[nodiscard]] result<const T *> try_get() const noexcept {
    if (!m_initialized) [[unlikely]] {
      return unexpected(error::not_initialized);
    }
    return &m_storage;
  }

private:
  struct dummy {};

  union {
    dummy m_dummy;
    T m_storage;
  };
  bool m_initialized;
};

template <is_fallible_initializable T> class fallible_allocated {
public:
  constexpr fallible_allocated(fallible_allocator &alloc) noexcept
      : m_alloc(&alloc), m_ptr(nullptr) {}

  fallible_allocated(const fallible_allocated &) = delete;
  fallible_allocated &operator=(const fallible_allocated &) = delete;

  fallible_allocated(fallible_allocated &&other) noexcept
      : m_alloc(other.m_alloc), m_ptr(other.m_ptr) {
    other.m_ptr = nullptr;
  }

  fallible_allocated &operator=(fallible_allocated &&other) noexcept {
    if (this != &other) {
      if (m_ptr) {
        m_ptr->~T();
        m_alloc->deallocate(m_ptr, sizeof(T));
      }

      // Transfer ownership
      m_alloc = other.m_alloc;
      m_ptr = other.m_ptr;

      other.m_ptr = nullptr;
    }
    return *this;
  }

  ~fallible_allocated() {
    if (m_ptr) {
      m_ptr->~T();
      m_alloc->deallocate(m_ptr, sizeof(T));
    }
  }

  result<void> try_init() noexcept {
    if (m_ptr)
      return {};

    auto alloc_res = m_alloc->allocate(sizeof(T), alignof(T));
    if (!alloc_res)
      return unexpected(alloc_res.error());

    m_ptr = reinterpret_cast<T *>(alloc_res->ptr);

    new (m_ptr) T(detail::constructor_key<T>{});

    auto init_res = m_ptr->try_init(detail::constructor_key<T>{});
    if (!init_res) {
      m_ptr->~T();
      m_alloc->deallocate(m_ptr, sizeof(T));
      m_ptr = nullptr;
      return init_res;
    }

    return {};
  }

  [[nodiscard]] result<T *> try_get() const noexcept {
    if (!m_ptr) [[unlikely]] {
      return unexpected(error::not_initialized);
    }
    return m_ptr;
  }

  [[nodiscard]] T *unsafe_get() const noexcept { return m_ptr; }

  [[nodiscard]] T *get() const noexcept {
    RELOCO_ASSERT(m_ptr, "Accessing fallible_allocated before try_init()");
    return m_ptr;
  }

  [[nodiscard]] T &operator*() const noexcept {
    RELOCO_ASSERT(m_ptr, "Dereferencing fallible_allocated before try_init()");
    return *m_ptr;
  }

  [[nodiscard]] T *operator->() const noexcept {
    RELOCO_ASSERT(m_ptr,
                  "Accessing member of fallible_allocated before try_init()");
    return m_ptr;
  }

private:
  fallible_allocator *m_alloc;
  T *m_ptr;
};

template <is_fallible_initializable T> class static_fallible_constructed {
public:
  constexpr static_fallible_constructed() noexcept
      : m_dummy{}, m_initialized(false) {}

  static_fallible_constructed(const static_fallible_constructed &) = delete;
  static_fallible_constructed &
  operator=(const static_fallible_constructed &) = delete;

  result<void> try_init() noexcept {
    if (m_initialized)
      return {};

    T *obj = new (&m_storage) T(detail::constructor_key<T>{});

    auto res = obj->try_init(detail::constructor_key<T>{});
    if (!res) {
      // If init fails, we destroy it once here.
      // After successful init, it lives forever.
      obj->~T();
      return res;
    }

    m_initialized = true;
    return {};
  }

  [[nodiscard]] T *unsafe_get() noexcept {
    return reinterpret_cast<T *>(&m_storage);
  }

  [[nodiscard]] const T *unsafe_get() const noexcept {
    return reinterpret_cast<const T *>(&m_storage);
  }

  [[nodiscard]] T *get() noexcept {
    RELOCO_ASSERT(m_initialized,
                  "Accessing static_fallible_constructed before try_init()");
    return reinterpret_cast<T *>(&m_storage);
  }

  [[nodiscard]] const T *get() const noexcept {
    RELOCO_ASSERT(m_initialized,
                  "Accessing static_fallible_constructed before try_init()");
    return reinterpret_cast<const T *>(&m_storage);
  }

  [[nodiscard]] T &operator*() noexcept {
    RELOCO_ASSERT(
        m_initialized,
        "Dereferencing static_fallible_constructed before try_init()");
    return *reinterpret_cast<T *>(&m_storage);
  }

  [[nodiscard]] const T &operator*() const noexcept {
    RELOCO_ASSERT(
        m_initialized,
        "Dereferencing static_fallible_constructed before try_init()");
    return *reinterpret_cast<const T *>(&m_storage);
  }

  [[nodiscard]] T *operator->() noexcept {
    RELOCO_ASSERT(
        m_initialized,
        "Accessing member of static_fallible_constructed before try_init()");
    return reinterpret_cast<T *>(&m_storage);
  }

  [[nodiscard]] const T *operator->() const noexcept {
    RELOCO_ASSERT(
        m_initialized,
        "Accessing member of static_fallible_constructed before try_init()");
    return reinterpret_cast<const T *>(&m_storage);
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return m_initialized;
  }

  [[nodiscard]] result<T *> try_get() noexcept {
    if (!m_initialized) [[unlikely]] {
      return unexpected(error::not_initialized);
    }
    return reinterpret_cast<T *>(&m_storage);
  }

  [[nodiscard]] result<const T *> try_get() const noexcept {
    if (!m_initialized) [[unlikely]] {
      return unexpected(error::not_initialized);
    }
    return reinterpret_cast<const T *>(&m_storage);
  }

private:
  struct dummy {};
  union {
    dummy m_dummy;
    // alignas ensures the buffer is correctly positioned for T
    alignas(T) char m_storage[sizeof(T)];
  };
  bool m_initialized;
};

template <typename T>
struct is_relocatable<fallible_constructed<T>> : is_relocatable<T> {};

template <typename T>
struct is_relocatable<fallible_allocated<T>> : std::true_type {};

} // namespace reloco
