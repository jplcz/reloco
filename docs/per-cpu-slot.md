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

## Tests

`tests/test_per_cpu_slot.cpp`, built once per hosted model (`jplcz_reloco_per_cpu_single_tests`,
`jplcz_reloco_per_cpu_thread_local_tests`).
