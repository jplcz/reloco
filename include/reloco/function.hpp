#pragma once
#include <functional>
#include <reloco/allocator.hpp>

namespace reloco {

template <typename Sig> class function;

template <typename R, typename... Args>
class [[nodiscard]] function<R(Args...)> {
public:
  static constexpr size_t SOO_SIZE = 32; // Adjustable inline buffer

  template <typename F>
  static result<function> try_allocate(F func,
                                       fallible_allocator &alloc) noexcept {
    using DecayedF = std::decay_t<F>;
    function f;
    f.m_alloc = &alloc;

    if constexpr (std::is_convertible_v<F, R (*)(Args...)>) {
      f.m_storage.func_ptr =
          reinterpret_cast<void *>(static_cast<R (*)(Args...)>(func));
      f.m_vtable = &c_func_vtable_factory<DecayedF, R, Args...>::c_func_vtable;
    } else if constexpr (sizeof(DecayedF) <= SOO_SIZE &&
                         alignof(DecayedF) <= alignof(std::max_align_t)) {
      // Fits in SOO buffer
      new (&f.m_storage.buffer) DecayedF(std::forward<F>(func));
      f.m_vtable = &function_vtable_factory<DecayedF, R, Args...>::soo_instance;
    } else {
      // Requires heap allocation
      auto res = alloc.allocate(sizeof(DecayedF), alignof(DecayedF));
      if (!res)
        return unexpected(error::allocation_failed);

      new (res->ptr) DecayedF(std::forward<F>(func));

      f.m_storage.heap_ptr = res->ptr;
      f.m_vtable =
          &function_vtable_factory<DecayedF, R, Args...>::heap_instance;
    }
    return f;
  }

  template <typename F> static result<function> try_create(F func) noexcept {
    return try_allocate(std::move(func), get_default_allocator());
  }

  R operator()(Args... args) const {
    RELOCO_ASSERT(m_vtable && "Attempted to call empty fallible_function");
    return m_vtable->invoke(&m_storage, std::forward<Args>(args)...);
  }

  ~function() noexcept {
    if (m_vtable)
      m_vtable->destroy(&m_storage, *m_alloc);
  }

  function(const function &) = delete;
  function &operator=(const function &) = delete;

  function(function &&other) noexcept
      : m_vtable(other.m_vtable), m_alloc(other.m_alloc) {
    if (m_vtable) {
      m_vtable->move_and_destroy(&other.m_storage, &m_storage);
    }

    other.m_vtable = nullptr;
    other.m_alloc = nullptr;
  }

  function &operator=(function &&other) noexcept {
    if (this != &other) {
      if (m_vtable) {
        m_vtable->destroy(&m_storage, *m_alloc);
      }

      m_vtable = other.m_vtable;
      m_alloc = other.m_alloc;

      if (m_vtable) {
        m_vtable->move_and_destroy(&other.m_storage, &m_storage);
      }

      other.m_vtable = nullptr;
      other.m_alloc = nullptr;
    }
    return *this;
  }

  result<function> try_clone() const noexcept {
    if (!m_vtable || !m_vtable->try_clone) {
      return unexpected(error::unsupported_operation);
    }

    // Attempt the fallible allocation part
    auto res = m_vtable->try_clone(&m_storage, *m_alloc);
    if (!res)
      return unexpected(res.error());

    function cloned;
    cloned.m_alloc = m_alloc;
    cloned.m_vtable = m_vtable;

    if (res.value() == (void *)0x1) {
      RELOCO_ASSERT(m_vtable->copy_soo != nullptr, "Internal logic error");
      m_vtable->copy_soo(&m_storage, &cloned.m_storage);
    } else {
      cloned.m_storage.heap_ptr = res.value();
    }

    return cloned;
  }

private:
  struct storage {
    union {
      alignas(std::max_align_t) std::byte buffer[SOO_SIZE];
      void *heap_ptr;
      void *func_ptr;
    };
  } m_storage;

  enum class dispatch_type : uint8_t {
    empty,     // No function assigned
    c_pointer, // Raw static function pointer (Zero allocation)
    soo,       // Small Object Optimization (Inline capture)
    heap       // External allocation (Large capture)
  };

  struct vtable {
    dispatch_type type;
    R (*invoke)(const storage *, Args...);
    void (*destroy)(storage *, fallible_allocator &);
    void (*move_and_destroy)(storage *src, storage *dest);
    result<void *> (*try_clone)(const storage *, fallible_allocator &);
    void (*copy_soo)(const storage *src, storage *dest);
  };

  template <typename F, typename R1, typename... Args1>
  struct c_func_vtable_factory {
    static constexpr vtable c_func_vtable = {
        .type = dispatch_type::c_pointer,
        .invoke = [](const storage *s, Args... args) -> R {
          // Direct call: No indirection through a captured objectx
          auto fp = reinterpret_cast<R (*)(Args...)>(s->func_ptr);
          return fp(std::forward<Args>(args)...);
        },
        .destroy = [](storage *, fallible_allocator &) { /* No-op */ },
        .move_and_destroy =
            [](storage *src, storage *dest) { dest->func_ptr = src->func_ptr; },
        .try_clone = [](const auto *, fallible_allocator &) -> result<void *> {
          return (void *)0x1; // Raw pointers don't need allocation
        },
        .copy_soo =
            [](const storage *src, storage *dest) {
              // For C pointers, "cloning" is just a bitwise copy of the pointer
              dest->func_ptr = src->func_ptr;
            },
    };
  };

  template <typename F, typename R1, typename... Args1>
  struct function_vtable_factory {
    static constexpr typename function<R1(Args1...)>::vtable soo_instance = {
        .type = dispatch_type::soo,
        .invoke = [](const auto *s, Args1... args) -> R {
          return (*reinterpret_cast<const F *>(s->buffer))(
              std::forward<Args>(args)...);
        },
        .destroy =
            [](auto *s, fallible_allocator &) {
              reinterpret_cast<F *>(s->buffer)->~F();
            },
        .move_and_destroy =
            [](auto *src, auto *dest) {
              new (dest->buffer)
                  F(std::move(*reinterpret_cast<F *>(src->buffer)));
              reinterpret_cast<F *>(src->buffer)->~F();
            },
        .try_clone = [](const auto *s, fallible_allocator &) -> result<void *> {
          if constexpr (std::is_nothrow_copy_constructible_v<F>) {

            // No allocation needed for SOO, but we return a "fake" pointer
            // to signal that the data is ready for the SOO buffer
            return (void *)0x1;
          } else {
            return unexpected(error::unsupported_operation);
          }
        },
        .copy_soo =
            [](const storage *src, storage *dest) {
              if constexpr (std::is_nothrow_copy_constructible_v<F>) {
                const F &src_obj = *reinterpret_cast<const F *>(src->buffer);
                // Guaranteed noexcept by our try_clone constraints
                new (dest->buffer) F(src_obj);
              } else {
                RELOCO_ASSERT(false);
              }
            },
    };

    static constexpr typename function<R1(Args1...)>::vtable heap_instance = {
        .type = dispatch_type::heap,
        .invoke = [](const auto *s, Args1... args) -> R {
          return (*static_cast<const F *>(s->heap_ptr))(
              std::forward<Args>(args)...);
        },
        .destroy =
            [](auto *s, fallible_allocator &alloc) {
              static_cast<F *>(s->heap_ptr)->~F();
              alloc.deallocate(s->heap_ptr, sizeof(F));
            },
        .move_and_destroy =
            [](auto *src, auto *dest) {
              dest->heap_ptr = src->heap_ptr;
              src->heap_ptr = nullptr;
            },
        .try_clone = [](const auto *s,
                        fallible_allocator &alloc) -> result<void *> {
          if constexpr (std::is_nothrow_copy_constructible_v<F>) {

            const F &original = *static_cast<const F *>(s->heap_ptr);
            auto res = alloc.allocate(sizeof(F), alignof(F));
            if (!res)
              return unexpected(res.error());
            return new (res->ptr) F(original);
          } else {
            return unexpected(error::unsupported_operation);
          }
        },
        .copy_soo = nullptr,
    };
  };

  const vtable *m_vtable = nullptr;
  fallible_allocator *m_alloc = nullptr;

  function() = default; // Private to force try_create
};

template <typename R, typename... Args>
class [[nodiscard]] function<R (*)(Args...)> {
  using FuncPtr = R (*)(Args...);
  FuncPtr m_ptr = nullptr;

public:
  static constexpr result<function>
  try_allocate(FuncPtr fp, fallible_allocator &) noexcept {
    return function(fp);
  }

  static constexpr result<function> try_create(FuncPtr fp) noexcept {
    return function(fp);
  }

  result<function> try_clone() const noexcept { return *this; }

  R operator()(Args... args) const {
    RELOCO_ASSERT(m_ptr, "Call to null function pointer");
    return m_ptr(std::forward<Args>(args)...);
  }

  function(const function &other) noexcept : m_ptr(other.m_ptr) {}

  function(function &&other) noexcept : m_ptr(other.m_ptr) {
    other.m_ptr = nullptr;
  }

private:
  explicit function(FuncPtr fp) : m_ptr(fp) {}
};

} // namespace reloco
