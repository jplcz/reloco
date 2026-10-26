# kernel

Bounds-proven building blocks for compute kernels (hot numeric loops). Requires C++20 or newer.

| Path | Contents |
| --- | --- |
| `kernel.hpp` | Umbrella include for the whole framework. |
| `config.hpp` | C++20 gate, `RELOCO_KERNEL_CHECKS` level, the per-level inline namespace and `RELOCO_KERNEL_CHECK`. |
| `index/` | `proven_index`, and the loop domains `domain`, `blocks`/`tail`, `pairs`, `windows`, `strided`. |

Everything lives in `reloco::kernel`; symbols are placed in an inline namespace named after the checks level, so
translation units built with different `RELOCO_KERNEL_CHECKS` values never share a definition.

"Kernel" means a compute kernel, not an operating system kernel.

Guides: `docs/kernel/README.md`. `view/` holds the tensor views (`docs/kernel/views.md`). `bind/` holds the span bindings (`docs/kernel/bind.md`). `project/` holds the zero-copy projections (`docs/kernel/projections.md`). `algo/` holds the core algorithms (`docs/kernel/algorithms.md`).
