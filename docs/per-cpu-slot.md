# `per_cpu_provider<T, Tag>` and `per_cpu_slot<T>`

`include/reloco/per_cpu_provider.hpp`, `include/reloco/per_cpu_slot.hpp`

Per-CPU storage for kernel-style code. The C++ side of kif's `static per_cpu` cells
(`docs/kif-proc-language.md` §4.17).

## Rules

- **No allocator.** Per-CPU data is built without allocating and without failing.
- **Zero is valid.** Blocks come zero-filled; a `per_cpu_slot` in zeroes is *empty*.
- **Never destroyed.** A CPU does not exit, so destructors do not run. `per_cpu_slot` has no `destroy()`.
- **Atomic context.** `get()` and slot access are `RELOCO_ATOMIC_ONLY`: the caller cannot migrate (preemption or
  interrupts off), so the CPU does not change under the reference. `reloco-tidy` checks this.

## `per_cpu_provider<T, Tag>`

| Member | Meaning |
|---|---|
| `get()` | The current CPU's `T &`. `RELOCO_ATOMIC_ONLY`. Never fails. |
| `cpu_count()` | Number of CPUs. |
| `get_cpu(i)` | CPU `i`'s block as `result<reference_wrapper<T>>`; `error::out_of_bounds` past the end. For bring-up and reports. |

`RELOCO_PER_CPU_MODEL` picks the backend:

| Model | Behaviour |
|---|---|
| `RELOCO_PER_CPU_MODEL_SINGLE` (default) | One global block: one "CPU". |
| `RELOCO_PER_CPU_MODEL_THREAD_LOCAL` | One block per thread; a thread stands in for a CPU (hosted concurrency tests). |
| `RELOCO_PER_CPU_MODEL_OS` | Includes `detail/porting/per_cpu_provider.hpp` (scaffold: `per_cpu_provider.template.hpp`, FreeBSD `uma_zalloc_pcpu(9)` + `zpcpu_get(9)`). |

## `per_cpu_slot<T>`

| Member | Meaning |
|---|---|
| `init(f)` | Build the object with `f` (`T(*)() noexcept`). Idempotent. Call once per CPU's block during bring-up. |
| `with(f)` / `with_mut(f)` | Run `f(const T &)` / `f(T &)`. `RELOCO_ATOMIC_ONLY`. Using an un-`init`ed slot trips `RELOCO_ASSERT`. |
| `is_ready()` | Whether `init` ran. |

## Example

```cpp
#include <reloco/per_cpu_provider.hpp>
#include <reloco/per_cpu_slot.hpp>

struct counters { std::uint64_t rx = 0, tx = 0; };
counters make_counters() noexcept { return counters{}; }   // init_fn: cannot allocate, cannot fail

// One block per module; every per-CPU cell of the module is a field of it.
struct module_block {
  reloco::per_cpu_slot<counters> stats;                    // zero == empty
};
struct module_tag {};                                      // distinguishes this module's block

using provider = reloco::per_cpu_provider<module_block, module_tag>;

void module_load() noexcept {                              // bring-up: every CPU's block, once
  for (std::size_t cpu = 0; cpu < provider::cpu_count(); ++cpu) {
    if (auto block = provider::get_cpu(cpu); block.has_value()) {   // out_of_bounds cannot happen here
      block->get().stats.init(&make_counters);
    }
  }
}

void on_rx() noexcept RELOCO_ATOMIC_ONLY {                 // caller did critical_enter(9): no migration
  provider::get().stats.with_mut([](counters &c) noexcept { c.rx++; });  // this CPU's counters, no lock
}
```

## Integration

- **Porting.** Copy `detail/porting/per_cpu_provider.template.hpp` to `per_cpu_provider.hpp`, adapt it, and set
  `JPLCZ_RELOCO_PORTING_HEADERS` (it defines `RELOCO_PER_CPU_MODEL_CUSTOM_OS`).
- **kif.** `static per_cpu` cells lower to one `module_block` per module and a `per_cpu_slot<T>` per cell. The
  generated module-load code runs `init` for every CPU. A per-CPU cell may not allocate or have a `drop` that matters
  (KIFP182/186).
- Cross-CPU reads of another CPU's data are the caller's responsibility; use atomics for anything read remotely.

- **FreeBSD `dpcpu(9)` variant.** `DPCPU_DEFINE` needs a literal identifier, so it cannot be stamped out from
  `per_cpu_provider<T, Tag>`; instead each pair is opted in with macros (scaffold:
  `detail/porting/per_cpu_provider_dpcpu.template.hpp`). No allocation, but the DPCPU area is small, so keep `T` small:

  ```cpp
  // net_stats.hpp: visible in every TU that uses the provider.
  struct net_stats { std::uint64_t rx; };
  struct net_tag {};                                 // Tag: plain identifier, pasted into the variable name
  RELOCO_DECLARE_DPCPU_PER_CPU(net_stats, net_tag)   // specializes per_cpu_provider + DPCPU_DECLARE

  // net_stats.cpp: exactly one TU (a header would give each TU its own copy).
  RELOCO_DEFINE_DPCPU_PER_CPU_TRIVIAL(net_stats, net_tag)  // DPCPU_DEFINE: zero-filled storage

  // Use: atomic context only, so the CPU cannot change under the reference.
  critical_enter();
  per_cpu_provider<net_stats, net_tag>::get().rx++;
  critical_exit();
  ```

  A `T` with a non-trivial ctor needs no special handling: the macros choose the stored type at compile time. A
  trivially default-constructible `T` is stored as-is. Anything else is stored as raw aligned bytes, and
  `RELOCO_DEFINE_DPCPU_PER_CPU_NONTRIVIAL` (use `_TRIVIAL` for the first case; a `static_assert` rejects the wrong one) also emits a `SYSINIT` (at `SI_SUB_SMP`) that constructs `T` on every CPU and a
  `SYSUNINIT` that destroys them, so `get()` stays a branch-free pointer computation and returns `T &` either way.
  Using the provider before that `SYSINIT` has run is invalid, and the ctor must be `noexcept` and non-sleeping:

  ```cpp
  struct widget { widget() noexcept : ready(true) {} bool ready; void poke() noexcept; };  // non-trivial ctor
  struct widget_tag {};
  RELOCO_DECLARE_DPCPU_PER_CPU(widget, widget_tag)   // header
  RELOCO_DEFINE_DPCPU_PER_CPU_NONTRIVIAL(widget, widget_tag)  // exactly one .cpp: storage + per-CPU init/uninit SYSINITs

  critical_enter();
  per_cpu_provider<widget, widget_tag>::get().poke();   // already constructed on every CPU
  critical_exit();
  ```

  How it is built: all provider logic (`get()`, `get_cpu()`, `cpu_count()`, `init_all()`/`fini_all()`, and the
  `sysinit`/`sysuninit` entry points passed to `SYSINIT`/`SYSUNINIT`) lives once in the template
  `detail::per_cpu_provider_base<T, Derived>`. A template cannot name a `DPCPU_DEFINE`d variable (it needs a literal
  identifier), so `RELOCO_DECLARE_DPCPU_PER_CPU` generates only a `Derived` that supplies two accessors, `local()`
  (`DPCPU_PTR`) and `of(cpu)` (`DPCPU_ID_PTR`). To hook the init into a different `SYSINIT` order or your own
  module-load path, call `per_cpu_provider<T, Tag>::init_all()` yourself instead of using `_NONTRIVIAL`'s `SYSINIT`
  and define the storage by hand with
  `DPCPU_DEFINE(reloco::detail::dpcpu_storage_t<T>, reloco_dpcpu_<Tag>)` (the `_TRIVIAL` macro's `static_assert`
  would reject a non-trivial `T`).

## Tests

`tests/test_per_cpu_slot.cpp`, built once per hosted model (`jplcz_reloco_per_cpu_single_tests`,
`jplcz_reloco_per_cpu_thread_local_tests`).
