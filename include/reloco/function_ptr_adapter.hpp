// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file function_ptr_adapter.hpp
 * @brief Compile-time proxy producing a plain C function pointer that strips
 * trailing arguments before forwarding to a bound plain function.
 *
 * `reloco::as_function_ptr<Fn>` solves a narrower problem than `function_ref`:
 * some C APIs expect an actual plain function pointer (e.g. `void (*)(int, int,
 * int, int)`) with no room for a side-channel context argument, while the callback
 * a caller wants to install only cares about a leading prefix of those parameters
 * (e.g. `void foo(int, int)`). There is no runtime state to carry the difference in
 * arity -- it must be baked into a distinct, per-binding, context-free trampoline
 * function generated at compile time.
 *
 * `Fn` is therefore taken as a non-type template argument (mirroring `nontype<Fn>`
 * in `function_ref.hpp`), not a runtime parameter: only a compile-time-known, plain
 * (capture-free) function pointer can be adapted this way, since the generated
 * trampoline is itself a plain function with no captured payload to smuggle through.
 * Stateful callables (lambdas with captures, functors) are deliberately rejected --
 * `function_ref`/`def_function_ref` are the right tools when a context pointer is
 * available at the call site.
 *
 * `as_function_ptr<Fn>` is an empty, stateless proxy object with a templated
 * conversion operator to `R (*)(TargetArgs...)`, for any `TargetArgs...` that is at
 * least as long as `Fn`'s own parameter list and whose leading prefix `Fn` can be
 * invoked with. The target signature is deduced from the conversion context (the
 * destination function pointer's type), so the common case needs no explicit
 * signature spelled out at the call site:
 *
 * @code
 * void foo(int a, int b) { ... }
 *
 * void (*f)(int, int, int, int) = reloco::as_function_ptr<foo>;
 * f(1, 2, 3, 4); // calls foo(1, 2); the trailing 3, 4 are discarded
 * @endcode
 */

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace reloco {

namespace detail {

template <typename Fn> struct plain_function_pointer_arity; // Primary template undefined

template <typename R, typename... Args> struct plain_function_pointer_arity<R (*)(Args...)> {
  static constexpr std::size_t value = sizeof...(Args);
};

// Splits a function *type* `R(TargetArgs...)` (as opposed to a function *pointer*
// type) into its return type and argument tuple, so a destination function
// pointer's signature -- deduced via `operator Signature *()` below -- can be
// inspected for arity/invocability before the trampoline is instantiated.
template <typename Signature> struct function_signature_traits; // Primary template undefined

template <typename R, typename... TargetArgs> struct function_signature_traits<R(TargetArgs...)> {
  using return_type = R;
  using args_tuple = std::tuple<TargetArgs...>;
  static constexpr std::size_t arity = sizeof...(TargetArgs);
};

// Checks that `Fn` can be invoked (returning `R`) with just the leading `sizeof...(I)`
// types out of `TargetArgsTuple`, used to produce a clearer static_assert than letting
// the mismatch surface from inside the generated trampoline's body instead.
template <typename R, typename Fn, typename TargetArgsTuple, std::size_t... I>
constexpr bool is_invocable_with_prefix(std::index_sequence<I...>) {
  return std::is_invocable_r_v<R, Fn, std::tuple_element_t<I, TargetArgsTuple>...>;
}

} // namespace detail

/**
 * @brief Compile-time proxy binding a plain (capture-free) function pointer @p Fn,
 * convertible to any wider plain function pointer type `R (*)(TargetArgs...)` whose
 * leading parameters `Fn` can be invoked with -- the trailing `TargetArgs` are
 * accepted and silently discarded by the generated trampoline.
 *
 * @tparam Fn A non-type template argument denoting a plain function (a function
 * name, which decays to a pointer, or any other converted-constant-expression
 * function pointer value). Never a lambda with captures or a functor: those have no
 * way to smuggle state through a context-free plain function pointer.
 */
template <auto Fn> class function_ptr_adapter {
private:
  using fn_type = decltype(Fn);

  static_assert(std::is_pointer_v<fn_type> && std::is_function_v<std::remove_pointer_t<fn_type>>,
                "function_ptr_adapter only binds plain, capture-free function pointers -- "
                "use function_ref/def_function_ref for lambdas or functors");

  static constexpr std::size_t arity = detail::plain_function_pointer_arity<fn_type>::value;

  // Generates the actual plain-function trampoline for one target signature `R
  // (TargetArgs...)`. A nested class template (rather than a function template taking
  // `R, TargetArgs...` directly) is used purely so the target signature can be split
  // into `R` and `TargetArgs...` in one step via partial specialization.
  template <typename Signature> struct trampoline_for; // Primary template undefined

  template <typename R, typename... TargetArgs> struct trampoline_for<R(TargetArgs...)> {
    // Only the leading `arity` of the (possibly longer) `TargetArgs...` pack are ever
    // forwarded to `Fn`; the rest are bound as ordinary parameters and simply dropped.
    template <std::size_t... I> static R invoke_prefix(std::index_sequence<I...>, TargetArgs... args) {
      return std::invoke(Fn, std::get<I>(std::forward_as_tuple(std::forward<TargetArgs>(args)...))...);
    }

    static R call(TargetArgs... args) {
      return invoke_prefix(std::make_index_sequence<arity>{}, std::forward<TargetArgs>(args)...);
    }
  };

public:
  constexpr function_ptr_adapter() noexcept = default;

  /**
   * @brief Produces the trampoline, deducing the target signature from the
   * destination function pointer type at the conversion/assignment site.
   */
  template <typename Signature, typename = std::enable_if_t<std::is_function_v<Signature>>>
  constexpr operator Signature *() const noexcept {
    using traits = detail::function_signature_traits<Signature>;
    static_assert(arity <= traits::arity,
                  "target function pointer type has fewer parameters than the bound function requires");
    static_assert(detail::is_invocable_with_prefix<typename traits::return_type, fn_type, typename traits::args_tuple>(
                      std::make_index_sequence<arity>{}),
                  "bound function is not invocable with the target function pointer's leading parameters");
    return &trampoline_for<Signature>::call;
  }
};

/** @brief Convenience variable template for `function_ptr_adapter<Fn>{}`, e.g.
 * `void (*f)(int, int, int, int) = reloco::as_function_ptr<foo>;`. */
template <auto Fn> inline constexpr function_ptr_adapter<Fn> as_function_ptr{};

} // namespace reloco
