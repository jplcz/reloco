// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file def_function_ref.hpp
 * @brief Partially-applies and owns a `function_ref`'s first argument.
 *
 * `reloco::def_function_ref<R(Arg0, Args...), GeneratorCapacity>` wraps a borrowed
 * `function_ref<R(Arg0, Args...)>` together with an *owned* source for its first
 * argument, and exposes a reduced `R(Args...)` call signature -- the first argument
 * is substituted automatically on every call.
 *
 * Unlike `function_ref` itself (purely non-owning, two pointers), the first-argument
 * source here is always stored *inside* the object: either a fixed value (captured by
 * a small generator closure) or a caller-supplied zero-argument generator callable
 * invoked fresh on every call, both held inline in a `reloco::inplace_function<Arg0(),
 * GeneratorCapacity>` -- never heap-allocated, so a `def_function_ref` only ever needs
 * to live on the stack.
 *
 * Because it exposes `operator()(Args...)`, a named `def_function_ref` lvalue binds
 * directly to a `function_ref<R(Args...)>` parameter through that type's ordinary
 * stateful-callable constructor (see `function_ref.hpp`) -- no separate conversion
 * operator is needed, and the same "must be a named lvalue, not a temporary" rule
 * applies.
 *
 * Move-only (matching `inplace_function`'s own move-only ownership of its stored
 * generator) and constructed exclusively through the `from_value`/`from_generator`
 * static factories below, never a public constructor -- this keeps the "fixed value"
 * and "generator callable" cases unambiguous without relying on overload-resolution
 * tie-breaking between them.
 */

#include "function_ref.hpp"
#include "inplace_function.hpp"
#include "lifetime.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace reloco {

template <typename Signature, size_t GeneratorCapacity = 32> class def_function_ref; // Primary template undefined

template <typename R, typename Arg0, typename... Args, size_t GeneratorCapacity>
class def_function_ref<R(Arg0, Args...), GeneratorCapacity> {
private:
  function_ref<R(Arg0, Args...)> fn_;
  inplace_function<Arg0(), GeneratorCapacity> generator_;

  template <typename G>
  constexpr def_function_ref(function_ref<R(Arg0, Args...)> fn, G &&generator) noexcept(
      std::is_nothrow_constructible_v<inplace_function<Arg0(), GeneratorCapacity>, G &&>)
      : fn_(fn), generator_(std::forward<G>(generator)) {}

public:
  def_function_ref(const def_function_ref &) = delete;
  def_function_ref &operator=(const def_function_ref &) = delete;

  constexpr def_function_ref(def_function_ref &&) noexcept = default;
  constexpr def_function_ref &operator=(def_function_ref &&) noexcept = default;

  /**
   * @brief Binds @p fn together with a fixed, owned value for its first argument.
   *
   * @p value is moved once into a small generator closure stored inline (not
   * re-read from any external location), so later mutating the caller's original
   * value (if it was an lvalue) has no effect on subsequent calls.
   */
  [[nodiscard]] static constexpr def_function_ref
  from_value(function_ref<R(Arg0, Args...)> fn, Arg0 value) noexcept(std::is_nothrow_move_constructible_v<Arg0>) {
    return def_function_ref(
        fn,
        [v = std::move(value)]() mutable noexcept(std::is_nothrow_copy_constructible_v<Arg0>) -> Arg0 { return v; });
  }

  /**
   * @brief Binds @p fn together with an owned generator invoked fresh on every
   * call to produce its first argument.
   *
   * @p generator is moved into inline storage (see `inplace_function`); it is
   * never borrowed, so it must fit within `GeneratorCapacity` bytes.
   */
  template <typename G, typename = std::enable_if_t<std::is_invocable_r_v<Arg0, G &>>>
  [[nodiscard]] static constexpr def_function_ref
  from_generator(function_ref<R(Arg0, Args...)> fn, G generator) noexcept(std::is_nothrow_move_constructible_v<G>) {
    return def_function_ref(fn, std::move(generator));
  }

  /**
   * @brief Invokes the bound function, substituting a freshly-generated first
   * argument ahead of @p args.
   */
  R operator()(Args... args) { return fn_(generator_(), std::forward<Args>(args)...); }

  /**
   * @brief Explicit adapter to `function_ref<R(Args...)>`, for call sites
   * that want a named, discoverable conversion rather than relying on
   * `function_ref`'s own implicit stateful-callable constructor (which also
   * binds a named `def_function_ref` lvalue directly, without this method).
   *
   * `RELOCO_LIFETIMEBOUND`-annotated: the returned `function_ref` borrows
   * `*this`, so it must not outlive the `def_function_ref` it was obtained
   * from -- exactly like borrowing any other named lvalue callable.
   */
  [[nodiscard]] constexpr function_ref<R(Args...)> as_function_ref() & noexcept RELOCO_LIFETIMEBOUND {
    return function_ref<R(Args...)>(*this);
  }
};

} // namespace reloco
