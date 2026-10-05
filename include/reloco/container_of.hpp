// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file container_of.hpp
 * @brief `container_of<Member>(ptr)`, a C++ equivalent of the Linux
 * kernel's `container_of()` macro: recovers a pointer to the enclosing
 * object from a pointer to one of its members.
 *
 * `Member` is a pointer-to-data-member non-type template argument (e.g.
 * `&q_spinlock::lock_object`); the enclosing ("parent"/"owner") class is
 * deduced from `Member` itself, not passed separately, so the call site
 * only ever names the member:
 *
 * ```cpp
 * struct q_spinlock : private structo::sync::queue_spin_lock<kernel_lock_traits> {
 *   struct lock_object lock_object;
 * };
 *
 * // Some C callback only ever hands back `struct lock_object *` (e.g. a
 * // FreeBSD lock_class lc_lock/lc_unlock/lc_owner callback); recover the
 * // owning q_spinlock* from it:
 * extern "C" void lc_lock_cb(struct lock_object *lo) {
 *   q_spinlock *self = reloco::container_of<&q_spinlock::lock_object>(lo);
 *   // ...
 * }
 * ```
 *
 * Unlike the private `c_*_hook_access<T, Hook>` helpers every intrusive_c_*
 * adapter (`intrusive_c_list.hpp`, `intrusive_c_list_head.hpp`,
 * `intrusive_c_slist.hpp`, `intrusive_c_stailq.hpp`,
 * `intrusive_c_tailq.hpp`) builds internally for its own hook member, this
 * is a standalone, public utility for any member, not tied to one of
 * reloco's own intrusive container adapters -- typically used at the
 * boundary with a foreign (OS/kernel/C-library) API that only ever deals
 * in pointers to one particular member of a larger C++ object.
 *
 * Implemented, like those internal helpers, via a fake non-null pointer
 * (`8192`/`0x2000`, chosen to avoid aggressive sanitizer null-pointer
 * checks) rather than `offsetof()`, so it also works for members that
 * `offsetof()` itself cannot portably target (e.g. members of a
 * non-standard-layout class).
 */

#include "lifetime.hpp"
#include <cstddef>
#include <type_traits>

namespace reloco {

namespace detail {

// Splits a pointer-to-data-member type `Member Class::*` back into its
// `Class`/`Member` parts, so `container_of` below only ever has to name
// the member pointer itself.
template <typename T> struct member_pointer_traits;

template <typename Class, typename Member> struct member_pointer_traits<Member Class::*> {
  using class_type = Class;
  using member_type = Member;
};

} // namespace detail

/**
 * @brief The enclosing/"parent" class of the pointer-to-member `Member`,
 * e.g. `container_of_class_t<&q_spinlock::lock_object>` is `q_spinlock`.
 */
template <auto Member>
using container_of_class_t = typename detail::member_pointer_traits<decltype(Member)>::class_type;

/**
 * @brief The type of the member itself pointed to by `Member`, e.g.
 * `container_of_member_t<&q_spinlock::lock_object>` is `struct lock_object`.
 */
template <auto Member>
using container_of_member_t = typename detail::member_pointer_traits<decltype(Member)>::member_type;

/**
 * @brief Recovers a pointer to the `Member`-enclosing object from a
 * pointer to its `Member` subobject -- the C++ equivalent of Linux's
 * `container_of(member, Class, Member)` macro, but with `Class` deduced
 * from `Member` instead of named explicitly.
 *
 * `member` must actually point at the `Member` subobject of a real,
 * live `container_of_class_t<Member>` object (typically handed back by a
 * foreign API that only ever deals in `container_of_member_t<Member> *`);
 * passing any other pointer is undefined behavior, exactly like the
 * kernel macro it mirrors.
 */
template <auto Member>
[[nodiscard]] constexpr container_of_class_t<Member> *
container_of(container_of_member_t<Member> *member) noexcept {
  using class_type = container_of_class_t<Member>;

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  const auto offset = reinterpret_cast<std::ptrdiff_t>(&(reinterpret_cast<class_type *>(8192)->*Member)) - 8192;
  auto *addr = reinterpret_cast<std::byte *>(member) - offset;
  return reinterpret_cast<class_type *>(addr);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

/**
 * @brief `const`-qualified overload of `container_of<Member>(member)`.
 */
template <auto Member>
[[nodiscard]] constexpr const container_of_class_t<Member> *
container_of(const container_of_member_t<Member> *member) noexcept {
  using class_type = container_of_class_t<Member>;

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  const auto offset = reinterpret_cast<std::ptrdiff_t>(&(reinterpret_cast<class_type *>(8192)->*Member)) - 8192;
  const auto *addr = reinterpret_cast<const std::byte *>(member) - offset;
  return reinterpret_cast<const class_type *>(addr);
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

} // namespace reloco
