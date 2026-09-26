// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tls_provider.template.hpp
 * @brief Documentation-only scaffold for `RELOCO_TLS_MODEL_OS`.
 *
 * Never `#include`d by anything -- copy this file to
 * `detail/porting/tls_provider.hpp` (dropping `.template`), fill it in
 * for your actual target, and define `RELOCO_TLS_MODEL` to
 * `RELOCO_TLS_MODEL_OS` (see `reloco/tls_provider.hpp`/`reloco/
 * reloco_config.hpp`), or point the `JPLCZ_RELOCO_PORTING_HEADERS` CMake
 * variable at a directory containing your finished `tls_provider.hpp`
 * and let the build do both for you.
 *
 * Sketches, loosely, what a **FreeBSD kernel** port might look like.
 * FreeBSD's `struct thread` has no generic, per-`Tag` "thread-specific
 * data" slot built in (unlike pthreads' `pthread_key_create`), so this
 * example keeps one small, `mtx(9)`-protected singly-linked list per
 * `Tag` mapping `curthread` -> a `malloc(9)`-ed `T`, scanned linearly on
 * every `get()`/`set()`. This is illustrative, not exact, complete, or
 * remotely fast (a real port should index by something cheaper, e.g. a
 * fixed-size array sized off `mp_ncpus`/`maxproc` and a `curthread`-
 * derived index, or a proper hash table), and is not compiled or
 * exercised by this repository (which targets hosted userspace, not the
 * FreeBSD kernel proper).
 *
 * Every `Tag` used anywhere in the program gets its own implicit
 * instantiation of the `node`/`list_state` statics below (one list per
 * `Tag`, shared by every `T` -- mirroring the built-in `RELOCO_TLS_MODEL_
 * PTHREAD` backend's own per-`Tag` `detail::tls_key_holder<Tag>`), so no
 * further registration step is needed for a new `Tag`.
 */

#include <sys/param.h>

#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/systm.h>

namespace reloco {

template <typename T, typename Tag> struct tls_provider {
  [[nodiscard]] static result<std::reference_wrapper<T>> get(allocator_ref alloc = default_allocator()) noexcept {
    (void)alloc; // kernel storage below always comes from malloc(9), not `alloc`.
    mtx_lock(&list_state::lock);
    for (node *n = list_state::head; n != nullptr; n = n->next) {
      if (n->owner == curthread) {
        mtx_unlock(&list_state::lock);
        return std::ref(n->value);
      }
    }
    mtx_unlock(&list_state::lock);
    return unexpected(error::not_found); // no set() yet for this thread/Tag.
  }

  static result<void> set(T value, allocator_ref alloc = default_allocator()) noexcept {
    (void)alloc;
    mtx_lock(&list_state::lock);
    for (node *n = list_state::head; n != nullptr; n = n->next) {
      if (n->owner == curthread) {
        n->value = std::move(value);
        mtx_unlock(&list_state::lock);
        return {};
      }
    }
    auto *n = static_cast<node *>(malloc(sizeof(node), M_DEVBUF, M_NOWAIT));
    if (n == nullptr) {
      mtx_unlock(&list_state::lock);
      return unexpected(error::resource_exhausted);
    }
    n->owner = curthread;
    n->value = std::move(value);
    n->next = list_state::head;
    list_state::head = n;
    mtx_unlock(&list_state::lock);
    return {};
  }

private:
  struct node {
    struct thread *owner;
    T value;
    node *next;
  };

  // One shared list per (T, Tag) pair -- a real port would key this off
  // Tag alone (one list holding every T, or better, an array), not
  // duplicate it per T too; kept this way here only to avoid needing a
  // type-erased value slot in this short example.
  struct list_state {
    static inline struct mtx lock{};
    static inline node *head = nullptr;
  };
};

} // namespace reloco
