// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file function_ref.hpp
 * @brief Non-owning, zero-allocation borrow of a callable.
 *
 * `reloco::function_ref` provides a lightweight, type-erased wrapper for any callable
 * (lambdas, function pointers, functors, member functions) without taking ownership or
 * allocating memory.
 *
 * It stores exactly two pointers: a context payload and a trampoline function.
 * Because it is a non-owning view, its constructor enforces `RELOCO_LIFETIMEBOUND`
 * to statically prevent capturing temporaries (rvalues) that would immediately dangle.
 *
 * Modeled as an unconditionally valid borrow (like `value_ref`), it has no default
 * constructor and cannot be null.
 */

#include "detail/compat.hpp"
#include "lifetime.hpp"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace reloco {

template <typename Signature> class function_ref; // Primary template undefined

template <typename R, typename... Args> class RELOCO_POINTER function_ref<R(Args...)> {
private:
  // We use a union because strict ISO C++ forbids casting function pointers to `void*`.
  // This ensures UB-free handling of raw function pointers on all architectures.
  union payload {
    void *obj;
    void (*func)();
  };

  payload data_{nullptr};
  R (*callback_)(payload, Args...) = nullptr;

  template <typename Callable> static R object_trampoline(payload p, Args... args) {
    // Cast the erased void* back to the exact reference type it was constructed with
    using T = std::remove_reference_t<Callable>;
    return std::invoke(*static_cast<T *>(p.obj), std::forward<Args>(args)...);
  }

  template <typename FuncPtr> static R func_ptr_trampoline(payload p, Args... args) {
    // Cast the erased function pointer back to the exact signature
    return std::invoke(reinterpret_cast<FuncPtr>(p.func), std::forward<Args>(args)...);
  }

public:
  // function_ref must borrow a valid callable; it cannot be null.
  function_ref() = delete;

  constexpr function_ref(const function_ref &) noexcept = default;
  constexpr function_ref &operator=(const function_ref &) noexcept = default;

  /**
   * @brief Constructs a function_ref binding to a callable.
   *
   * RELOCO_LIFETIMEBOUND ensures that if `f` is a temporary (e.g., an inline lambda),
   * the function_ref cannot outlive the statement it was created in, catching
   * dangling references at compile time under Clang.
   */
  template <
      typename F, typename Decayed = std::decay_t<F>,
      typename = std::enable_if_t<!std::is_same_v<Decayed, function_ref> && std::is_invocable_r_v<R, F &&, Args...>>>
  constexpr function_ref(F &&f RELOCO_LIFETIMEBOUND) noexcept {
    // Case 1: Raw function pointers (passed by value)
    if constexpr (std::is_pointer_v<Decayed> && std::is_function_v<std::remove_pointer_t<Decayed>>) {
      data_.func = reinterpret_cast<void (*)()>(f);
      callback_ = &func_ptr_trampoline<Decayed>;
    }
    // Case 2: Function references (lvalues like `void my_func()`)
    else if constexpr (std::is_function_v<std::remove_reference_t<F>>) {
      data_.func = reinterpret_cast<void (*)()>(std::addressof(f));
      callback_ = &func_ptr_trampoline<std::add_pointer_t<std::remove_reference_t<F>>>;
    }
    // Case 3: Stateful lambdas, functors, and stateless lambdas (decayed to objects)
    else {
      // Cast away constness for opaque storage. The trampoline safely casts it back to `const T*`
      // if `F` was originally a const lvalue reference.
      data_.obj = const_cast<void *>(static_cast<const void *>(std::addressof(f)));
      callback_ = &object_trampoline<F>;
    }
  }

  /**
   * @brief Invokes the borrowed callable.
   */
  R operator()(Args... args) const { return callback_(data_, std::forward<Args>(args)...); }
};

} // namespace reloco