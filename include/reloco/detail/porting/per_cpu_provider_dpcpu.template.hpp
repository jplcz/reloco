// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file per_cpu_provider_dpcpu.template.hpp
 * @brief Documentation-only `RELOCO_PER_CPU_MODEL_OS` example for the **FreeBSD kernel**, built on `dpcpu(9)`
 * (`DPCPU_DEFINE`/`DPCPU_PTR`/`DPCPU_ID_PTR`) instead of `per_cpu_provider.template.hpp`'s UMA allocation.
 *
 * Never `#include`d by anything -- copy it to `detail/porting/per_cpu_provider.hpp` and adapt it. Not compiled by this
 * repository (which targets hosted userspace); check the `DPCPU_*` macros against your kernel's `sys/pcpu.h`.
 *
 * `DPCPU_DEFINE` needs a literal identifier and a linker-section placement, so `per_cpu_provider<T, Tag>` cannot be
 * generated from the template alone. Instead the primary template is only *declared*, and each `<T, Tag>` pair is
 * opted in explicitly with two macros:
 * - `RELOCO_DECLARE_DPCPU_PER_CPU(T, Tag)` in a header: specializes `per_cpu_provider<T, Tag>` and `DPCPU_DECLARE`s the
 *   storage. Must be visible in every TU that uses the provider (a missing one is a compile error, never a silent
 *   second copy).
 * - `RELOCO_DEFINE_DPCPU_PER_CPU_TRIVIAL(T, Tag)` / `RELOCO_DEFINE_DPCPU_PER_CPU_NONTRIVIAL(T, Tag)` in exactly one
 *   `.cpp`: `DPCPU_DEFINE` the storage; the NONTRIVIAL one also emits the per-CPU init/uninit `SYSINIT`s (below), and a
 *   `static_assert` rejects the wrong variant. Defining it in a header
 *   would give every TU its own variable (`DPCPU_DEFINE_STATIC` is file-local), so it is deliberately not offered.
 *
 * `Tag` must be a plain identifier (it is pasted into the variable name) naming a type declared at global or
 * `reloco`-visible scope. The macros must be used at global namespace scope.
 *
 * The contract is the same as the UMA variant: `get()` is atomic-context only (e.g. under `critical_enter(9)`), never
 * allocates or fails; the block is zero-filled (DPCPU storage without an initializer), so `T` must be valid when
 * all-zero; nothing is destroyed. Unlike the UMA variant, no allocation and no pointer indirection is needed, but
 * the DPCPU area is sized at boot with a small per-module budget (`DPCPU_MODMIN`), so keep `T` small.
 *
 * @code
 * // net_stats.hpp
 * struct net_stats { std::uint64_t rx; std::uint64_t tx; };
 * struct net_tag {};
 * RELOCO_DECLARE_DPCPU_PER_CPU(net_stats, net_tag)   // visible everywhere it is used
 *
 * // net_stats.cpp -- exactly one TU
 * RELOCO_DEFINE_DPCPU_PER_CPU_TRIVIAL(net_stats, net_tag)
 *
 * // users
 * critical_enter();
 * per_cpu_provider<net_stats, net_tag>::get().rx++;  // this CPU's block
 * critical_exit();
 * @endcode
 *
 * Non-trivial `T`: DPCPU storage is raw zero-filled memory (no ctor runs), so the macros pick the stored type from `T`
 * at compile time. A trivially default-constructible `T` is stored as-is (zero must be a valid state). Anything else
 * is stored as raw aligned bytes, and `RELOCO_DEFINE_DPCPU_PER_CPU_NONTRIVIAL` additionally emits a `SYSINIT(9)` that
 * placement-constructs `T` on every CPU (`CPU_FOREACH`) at `SI_SUB_SMP`, plus a `SYSUNINIT(9)` that destroys them
 * (matters for loadable modules). Callers never see the difference -- `per_cpu_provider<T, Tag>::get()` is `T &` --
 * and `get()` stays a plain pointer computation with no branch. In exchange: touching the provider before that
 * `SYSINIT` has run (early boot, an earlier `SYSINIT`) is invalid, `T`'s ctor must not sleep or fail, and a `T` that
 * needs a later subsystem must be given a later-ordered init by hand instead. Verify the `SI_SUB_*` choice against
 * your kernel (the DPCPU areas of all CPUs must already exist).
 *
 * @code
 * RELOCO_DECLARE_DPCPU_PER_CPU(widget, widget_tag)   // widget has a non-trivial ctor
 * RELOCO_DEFINE_DPCPU_PER_CPU_NONTRIVIAL(widget, widget_tag)  // one .cpp: storage + the init/uninit SYSINITs
 * critical_enter();
 * per_cpu_provider<widget, widget_tag>::get().poke();  // already constructed on every CPU
 * critical_exit();
 * @endcode
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <type_traits>

#include <sys/param.h>

#include <sys/kernel.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/systm.h>

namespace reloco {

namespace detail {

/** Raw, suitably aligned bytes for a non-trivial `T`; zero-filled until `init_all()` constructs `T` in place. */
template <typename T> struct dpcpu_raw_cell {
  alignas(T) unsigned char storage[sizeof(T)];
};

// Trivially default-constructible T: the zero-filled DPCPU block already is a valid T.
template <typename T, bool = std::is_trivially_default_constructible_v<T>> struct dpcpu_storage {
  using type = T;
  static T &get(T &cell) noexcept { return cell; }
  static void construct(T &) noexcept {}
  static void destroy(T &) noexcept {}
};

// Anything else: constructed by per_cpu_provider_base::init_all(), run from a SYSINIT.
template <typename T> struct dpcpu_storage<T, false> {
  static_assert(std::is_nothrow_default_constructible_v<T>, "per_cpu_provider<T>: T's ctor must be noexcept");
  using type = dpcpu_raw_cell<T>;
  static T &get(dpcpu_raw_cell<T> &cell) noexcept { return *std::launder(reinterpret_cast<T *>(cell.storage)); }
  static void construct(dpcpu_raw_cell<T> &cell) noexcept { ::new (static_cast<void *>(cell.storage)) T(); }
  static void destroy(dpcpu_raw_cell<T> &cell) noexcept { get(cell).~T(); }
};

template <typename T> using dpcpu_storage_t = typename dpcpu_storage<T>::type;

/** Everything a DPCPU-backed `per_cpu_provider<T, Tag>` does, written once as a template. The only thing a template
 * cannot do is name the `DPCPU_DEFINE`d variable (it needs a literal identifier), so the macro-generated `Derived`
 * supplies just two accessors:
 * - `static dpcpu_storage_t<T> &local() noexcept`      -- this CPU's cell (`DPCPU_PTR`)
 * - `static dpcpu_storage_t<T> &of(int cpu) noexcept`  -- a given CPU's cell (`DPCPU_ID_PTR`)
 * `init_all`/`fini_all`/`sysinit`/`sysuninit` are what `RELOCO_DEFINE_DPCPU_PER_CPU_NONTRIVIAL` hands to `SYSINIT`. */
template <typename T, typename Derived> struct per_cpu_provider_base {
  /** The calling CPU's block. Atomic context only; valid only after `init_all()` for a non-trivial `T`. */
  [[nodiscard]] RELOCO_ATOMIC_ONLY static T &get() noexcept { return dpcpu_storage<T>::get(Derived::local()); }

  [[nodiscard]] static std::size_t cpu_count() noexcept { return static_cast<std::size_t>(mp_ncpus); }

  /** Another CPU's block, for bring-up and reporting code that is not on that CPU. */
  [[nodiscard]] static result<std::reference_wrapper<T>> get_cpu(std::size_t cpu) noexcept {
    if (cpu >= cpu_count()) {
      return unexpected(error::out_of_bounds);
    }
    return std::ref(dpcpu_storage<T>::get(Derived::of(static_cast<int>(cpu))));
  }

  /** Constructs `T` on every CPU (no-op for a trivial `T`). */
  static void init_all() noexcept {
    int cpu;
    CPU_FOREACH(cpu) { dpcpu_storage<T>::construct(Derived::of(cpu)); }
  }

  /** Destroys `T` on every CPU (no-op for a trivial `T`). */
  static void fini_all() noexcept {
    int cpu;
    CPU_FOREACH(cpu) { dpcpu_storage<T>::destroy(Derived::of(cpu)); }
  }

  // The `void (*)(void *)` shape SYSINIT/SYSUNINIT require.
  static void sysinit(void *) noexcept { init_all(); }
  static void sysuninit(void *) noexcept { fini_all(); }
};

} // namespace detail

// Declared only: every <T, Tag> must be opted in via RELOCO_DECLARE_DPCPU_PER_CPU.
template <typename T, typename Tag> struct per_cpu_provider;

} // namespace reloco

#define RELOCO_DPCPU_NAME_(Tag) reloco_dpcpu_##Tag

// Header: specializes the provider on top of per_cpu_provider_base, supplying only the two DPCPU accessors.
#define RELOCO_DECLARE_DPCPU_PER_CPU(T, Tag)                                                                           \
  DPCPU_DECLARE(reloco::detail::dpcpu_storage_t<T>, RELOCO_DPCPU_NAME_(Tag));                                          \
  template <>                                                                                                          \
  struct reloco::per_cpu_provider<T, Tag>                                                                              \
      : reloco::detail::per_cpu_provider_base<T, reloco::per_cpu_provider<T, Tag>> {                                   \
    static reloco::detail::dpcpu_storage_t<T> &local() noexcept { return *DPCPU_PTR(RELOCO_DPCPU_NAME_(Tag)); }        \
    static reloco::detail::dpcpu_storage_t<T> &of(int cpu) noexcept {                                                  \
      return *DPCPU_ID_PTR(cpu, RELOCO_DPCPU_NAME_(Tag));                                                              \
    }                                                                                                                  \
  }

// For a trivially default-constructible T: storage only (the zero-filled block already is a valid T). The
// static_assert catches a T that needs the _NONTRIVIAL variant, which would otherwise silently never be constructed.
#define RELOCO_DEFINE_DPCPU_PER_CPU_TRIVIAL(T, Tag)                                                                    \
  static_assert(std::is_trivially_default_constructible_v<T>,                                                          \
                "T is not trivially default-constructible: use RELOCO_DEFINE_DPCPU_PER_CPU_NONTRIVIAL");               \
  DPCPU_DEFINE(reloco::detail::dpcpu_storage_t<T>, RELOCO_DPCPU_NAME_(Tag))

// For any other T: storage plus a SYSINIT/SYSUNINIT pair that runs the provider's init_all()/fini_all(). The alias
// keeps the template's comma out of SYSINIT's macro arguments.
#define RELOCO_DEFINE_DPCPU_PER_CPU_NONTRIVIAL(T, Tag)                                                                 \
  static_assert(!std::is_trivially_default_constructible_v<T>,                                                         \
                "T is trivially default-constructible: use RELOCO_DEFINE_DPCPU_PER_CPU_TRIVIAL");                      \
  DPCPU_DEFINE(reloco::detail::dpcpu_storage_t<T>, RELOCO_DPCPU_NAME_(Tag));                                           \
  using reloco_dpcpu_provider_##Tag = reloco::per_cpu_provider<T, Tag>;                                                \
  SYSINIT(reloco_dpcpu_init_##Tag, SI_SUB_SMP, SI_ORDER_ANY, reloco_dpcpu_provider_##Tag::sysinit, nullptr);           \
  SYSUNINIT(reloco_dpcpu_fini_##Tag, SI_SUB_SMP, SI_ORDER_ANY, reloco_dpcpu_provider_##Tag::sysuninit, nullptr)
