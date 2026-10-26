# Kernel checks: `RELOCO_KERNEL_CHECKS`

## Why a separate switch?

reloco normally asserts aggressively. In a hot compute loop that would defeat the purpose: an assertion per element
stops vectorization. So the kernel framework splits safety in two:

* **Outer boundary, always on in every build.** Validation that happens once per call (sizes match, offsets do not
  overflow, regions do not overlap) and returns an `error`. It costs a handful of integer operations per kernel
  call, not per element.
* **Inner loop, opt in.** Re-verifies that every proof the framework handed out is actually true. This is for
  catching bugs *in the framework or in a pattern you wrote*, so you enable it in tests, not in production.

The default is **off, even in Debug builds**. A Debug build must not silently slow your kernels down.

## Levels

| `RELOCO_KERNEL_CHECKS` | What happens | Use it for |
| --- | --- | --- |
| `0` (default) | No inner checks. Accessors compile to plain loads and stores. | Production, benchmarks. |
| `1` | Every proof the framework mints asserts its invariant: a dereferenced `end()` iterator, a block that would run past the extent, a window that does not fit. | Unit tests, CI, sanitizer and fuzz builds. |
| `2` | Level 1 plus heavier checks added by the binding layer (reserved; behaves like `1` today). | Debugging one failing kernel. |

Any other value is a compile error.

## Enabling it

```cmake
# CMake: enable for one target (for example your tests), leave the rest at the default.
target_compile_definitions(my_kernel_tests PRIVATE RELOCO_KERNEL_CHECKS=1)
```

```cpp
// Or on the command line / before including any kernel header:
//   -DRELOCO_KERNEL_CHECKS=1
#include <reloco/kernel/kernel.hpp>

// The chosen level is available as a constant, for tests that behave differently per level.
static_assert(reloco::kernel::checks_level == 1);
```

A failed check goes through `RELOCO_ASSERT`, so it uses whatever assert handler you configured (including the
`RELOCO_KERNEL_PANIC` hook on bare metal).

## What a result must not depend on

Checks only abort when a proof is violated. A correct program produces **identical results at every level**. reloco
runs its own kernel tests at level 0 and level 1 to prove that the unchecked path behaves exactly like the checked
one.

## Mixing levels in one program (ODR)

Header-only code that changes with a macro is a classic source of silent bugs: two translation units compile the
"same" function differently and the linker keeps one at random (an ODR violation).

To prevent that, every kernel symbol lives in an **inline namespace named after the level**:
`reloco::kernel::checks_0`, `reloco::kernel::checks_1`, ... You write `reloco::kernel::domain(n)` as usual; the
compiler resolves it into the current level's namespace. Consequences:

* Two translation units built with different levels get **different symbols**, so each keeps its own correct code
  instead of colliding.
* A non-inline function that takes a kernel type in its signature but is declared at one level and defined at
  another fails to **link**, so a view cannot be passed across levels by accident.
* Object **layout never depends on the level**: `proven_index` is one `std::size_t` at every level, and checks only
  add code, never data. That is what keeps your own classes that embed kernel types safe across levels.

Rule of thumb: choose one level per target and apply it to everything in that target.

## Language level

The kernel headers require C++20 or newer and give a clear `#error` otherwise. They are only compiled in the
header check when the standard is C++20 or newer.
