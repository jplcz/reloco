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
 * Because it is a non-owning view, it is built from two separate converting
 * constructors rather than one universal-reference one:
 *
 *   - A plain function (a function pointer, or a bare function name, which
 *     decays to one) is bound *by value*. Functions have static storage
 *     duration and can never dangle, so no lifetime restriction applies.
 *   - A stateful callable (a lambda, functor, or any other object with
 *     `operator()`) is bound through a plain (non-forwarding) reference
 *     parameter, `F &f`. Because a non-forwarding reference parameter can
 *     only ever deduce against an lvalue -- never an rvalue -- this is a
 *     hard, portable (not Clang-specific) compile error for exactly the
 *     mistake that used to slip through silently on non-Clang compilers:
 *     binding `function_ref` to a temporary callable (e.g. an inline
 *     lambda) that is destroyed before the borrow is ever used. Naming the
 *     callable first (`auto cb = [...] { ... }; use(cb);`) is now required
 *     everywhere, not just a best-effort convention `RELOCO_LIFETIMEBOUND`
 *     happened to catch only under Clang.
 *
 * A bound member function (`&T::method` plus a `T` instance) is a third,
 * separate case: the member pointer itself does not generally fit in a
 * single pointer-sized slot (and so cannot be erased into `payload` the
 * way a plain function pointer can), so it is instead passed as a
 * compile-time, non-type template argument via the `nontype<&T::method>`
 * tag (mirroring the same `nontype_t` idiom standardized for
 * `std::function_ref`), leaving only the bound object's address to be
 * stored at runtime -- the two-pointer size invariant above is preserved.
 *
 * Modeled as an unconditionally valid borrow (like `value_ref`), it has no default
 * constructor and cannot be null.
 */

#include "detail/compat.hpp"
#include "lifetime.hpp"
#include "send_sync.hpp"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace reloco {

/** @brief Tag type carrying a compile-time, non-type template argument
 * (typically a member-function pointer such as `&T::method`), used to
 * bind `function_ref` to a member function without needing to store the
 * (generally larger-than-a-pointer) member-pointer value at runtime.
 * Default-constructed explicitly so `nontype<&T::method>` can be passed
 * as a plain tag argument without risk of an implicit conversion.
 */
template <auto Value> struct nontype_t {
  explicit nontype_t() = default;
};

/** @brief Convenience variable template for `nontype_t<Value>{}`, e.g.
 * `function_ref<int(int)>(nontype<&Foo::bar>, foo)`. */
template <auto Value> inline constexpr nontype_t<Value> nontype{};

template <typename Signature> class function_ref; // Primary template undefined

template <typename R, typename... Args>
class RELOCO_POINTER function_ref<R(Args...)> : private detail::requires_explicit_send_sync {
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

  // `MemPtr` is baked into the trampoline's own type rather than stored in `payload`,
  // so only the bound object's address needs to be erased at runtime.
  template <auto MemPtr, typename T> static R bound_member_trampoline(payload p, Args... args) {
    return std::invoke(MemPtr, *static_cast<T *>(p.obj), std::forward<Args>(args)...);
  }

  template <typename Fn>
  static constexpr bool is_plain_function_pointer_v =
      std::is_pointer_v<Fn> && std::is_function_v<std::remove_pointer_t<Fn>>;

public:
  // function_ref must borrow a valid callable; it cannot be null.
  function_ref() = delete;

  constexpr function_ref(const function_ref &) noexcept = default;
  constexpr function_ref &operator=(const function_ref &) noexcept = default;

  /**
   * @brief Binds a plain function: a function pointer, or a bare function
   * name (which decays to one through this by-value parameter). Taken by
   * value deliberately -- a function has static storage duration, so
   * there is no temporary to dangle and no reference/lifetime concern at
   * all, unlike the stateful-callable overload below.
   */
  template <typename Fn,
            typename = std::enable_if_t<is_plain_function_pointer_v<Fn> && std::is_invocable_r_v<R, Fn, Args...>>>
  constexpr function_ref(Fn f) noexcept {
    data_.func = reinterpret_cast<void (*)()>(f);
    callback_ = &func_ptr_trampoline<Fn>;
  }

  /**
   * @brief Binds a stateful callable (a lambda, functor, or other object
   * with `operator()`).
   *
   * Deliberately takes @p f through a plain, non-forwarding reference
   * parameter rather than a universal reference: template argument
   * deduction against `F &` can only ever succeed for an lvalue argument
   * (deducing `F` as the (possibly `const`-qualified) referred-to type),
   * never for an rvalue/temporary -- so passing a temporary callable
   * directly (e.g. an inline lambda) fails to compile on every compiler,
   * not just Clang (which `RELOCO_LIFETIMEBOUND` alone could catch).
   * `RELOCO_LIFETIMEBOUND` is still applied on top for the cases it can
   * additionally catch (e.g. a named-but-shorter-lived local escaping
   * through a returned `function_ref`).
   */
  template <typename F,
            typename = std::enable_if_t<!std::is_same_v<std::remove_cv_t<F>, function_ref> && !std::is_function_v<F> &&
                                        !is_plain_function_pointer_v<F> && std::is_invocable_r_v<R, F &, Args...>>>
  constexpr function_ref(F &f RELOCO_LIFETIMEBOUND) noexcept {
    // Cast away constness for opaque storage. The trampoline safely casts it back to `const T*`
    // if `F` was originally `const`-qualified.
    data_.obj = const_cast<void *>(static_cast<const void *>(std::addressof(f)));
    callback_ = &object_trampoline<F>;
  }

  /**
   * @brief Binds a member function `&T::method` (or any other
   * pointer-to-member-function value) to a bound object @p obj, e.g.
   * `function_ref<int(int)>(nontype<&Foo::bar>, foo)`.
   *
   * The member pointer is passed as a compile-time non-type template
   * argument (not a runtime value) precisely because a member-pointer's
   * own size is implementation-defined and generally larger than a single
   * pointer, so it cannot be erased into `payload` alongside the object
   * address without breaking the two-pointer size invariant; baking it
   * into the trampoline's type instead keeps the only runtime state to
   * `std::addressof(obj)`.
   *
   * Takes @p obj through a plain, non-forwarding reference for the same
   * reason as the stateful-callable overload above: deduction against
   * `T &` naturally supports binding either a mutable or a `const`
   * object (matching `MemPtr`'s own const-qualification) while
   * structurally rejecting any temporary object argument.
   */
  template <auto MemPtr, typename T,
            typename = std::enable_if_t<std::is_invocable_r_v<R, decltype(MemPtr), T &, Args...>>>
  constexpr function_ref(nontype_t<MemPtr>, T &obj RELOCO_LIFETIMEBOUND) noexcept {
    data_.obj = const_cast<void *>(static_cast<const void *>(std::addressof(obj)));
    callback_ = &bound_member_trampoline<MemPtr, T>;
  }

  /**
   * @brief Binds a free function (or any other non-member callable with
   * static storage duration) via `nontype<Fn>`, e.g.
   * `function_ref<int(int)>(nontype<my_func>)`.
   *
   * Equivalent to the plain-function-pointer overload above, but useful
   * when the target is already known at compile time and a tag-based
   * call site is preferred for symmetry with the bound-member overload
   * (e.g. generic code that always spells the binding as
   * `nontype<Target>` regardless of whether `Target` is a free function
   * or a member function).
   */
  template <auto Fn, typename = std::enable_if_t<std::is_invocable_r_v<R, decltype(Fn), Args...>>>
  constexpr function_ref(nontype_t<Fn>) noexcept {
    data_.func = reinterpret_cast<void (*)()>(Fn);
    callback_ = &func_ptr_trampoline<decltype(Fn)>;
  }

  /**
   * @brief Invokes the borrowed callable.
   */
  R operator()(Args... args) const { return callback_(data_, std::forward<Args>(args)...); }
};

/** @brief `function_ref<R(Args...)>`'s `operator()` is `const`-qualified
 * but its trampoline invokes the *borrowed* callable non-`const` (see
 * `object_trampoline` above), so it may freely mutate the referenced
 * callable's state -- exactly like `function<Sig>`'s own unsynchronized
 * interior mutability (see `function.hpp`), calling the same
 * `function_ref` concurrently from multiple threads is a data race
 * whenever the borrowed callable has any mutable state. Never `Sync`,
 * regardless of `Sig`. `is_send` is explicitly kept at `true`: a
 * `function_ref` is just two pointers, so moving it to another thread is
 * always fine -- whether the *referenced* callable itself is safe to keep
 * calling from that other thread is on the caller, exactly like any other
 * borrowed reference.
 */
template <typename R, typename... Args> struct is_send<function_ref<R(Args...)>> : std::true_type {};
/** @copydoc is_send<function_ref<R(Args...)>> */
template <typename R, typename... Args> struct is_sync<function_ref<R(Args...)>> : std::false_type {};

} // namespace reloco