# `detail/porting/`

This directory is reloco's fixed-path plug-in point for every
platform-specific "`_CUSTOM`" backend (see each header's own top-level
doc comment and `reloco_config.hpp` for the full rationale):

| `_CUSTOM` macro                    | Fixed include                        | Scaffold (ships here)         |
|-------------------------------------|---------------------------------------|--------------------------------|
| `RELOCO_MUTEX_BACKEND_CUSTOM`       | `detail/porting/mutex.hpp`            | `mutex.template.hpp`           |
| `RELOCO_THREAD_BACKEND_CUSTOM`      | `detail/porting/thread.hpp`           | `thread.template.hpp`          |
| `RELOCO_SPIN_LOCK_BACKEND_CUSTOM`   | `detail/porting/spin_lock.hpp`        | `spin_lock.template.hpp`       |
| `RELOCO_TLS_MODEL_OS`               | `detail/porting/tls_provider.hpp`     | `tls_provider.template.hpp`    |
| `RELOCO_FUTEX_BACKEND_CUSTOM`       | `detail/porting/futex.hpp`            | `futex.template.hpp`           |

Only the `*.template.hpp` files above ship in this repository -- they are
documentation-only scaffolds (each one sketches, loosely, what a FreeBSD
kernel port might look like; none is compiled or exercised by this
repository's own build/test suite) and are never `#include`d by anything.
The corresponding real header (`mutex.hpp`, `thread.hpp`, ...) is
`#include`d **unconditionally**, at a fixed path, by reloco's own header
of the same name (`reloco/mutex.hpp`, `reloco/thread.hpp`, ...) whenever
its matching `_CUSTOM` macro is defined -- right at the exact point the
built-in backend would otherwise have defined the same API. That means:

- Correctness never depends on *where else* the application/kernel
  happens to include its replacement from (the previous "included by the
  application through the normal path" convention required the
  replacement to already be visible before the *first* reloco header that
  references it -- effectively an include-order requirement that grows
  fragile as more reloco headers use the same primitive transitively).
  With a fixed include path, whichever reloco header reaches (say)
  `reloco::mutex` first triggers the same one `#include` at the same
  spot, with the exact same result, regardless of anything else.
- You do not have to place your files here by hand: set the
  `JPLCZ_RELOCO_PORTING_HEADERS` CMake variable (see the top-level
  `CMakeLists.txt`) to a directory containing any of `mutex.hpp`/
  `thread.hpp`/`spin_lock.hpp`/`tls_provider.hpp`/`futex.hpp` before
  configuring reloco (top-level build, or via `add_subdirectory`/
  `FetchContent`); the build copies whichever of those files exist there
  into this directory (in the build tree, then installed alongside
  reloco's own headers) and defines the matching `RELOCO_*_BACKEND_
  CUSTOM`/`RELOCO_TLS_MODEL` macro on the `jplcz_reloco` INTERFACE target
  automatically -- for every consumer linking against it, including a
  downstream `find_package(jplcz_reloco)` consumer, via the exported
  target's own INTERFACE properties.
- If you are not using CMake (or prefer to manage it yourself), just copy
  the relevant `*.template.hpp` scaffold to its non-`.template` name in
  this same directory, fill it in, and define the matching macro
  yourself (via a compiler `-D` flag or `reloco_user_config.hpp`, see
  `reloco_config.hpp`).

A file placed here **never replaces** any of reloco's own headers --
each real reloco header (`mutex.hpp`, `thread.hpp`, ...) always exists
and is always the one an application/kernel `#include`s; a
`detail/porting/*.hpp` file only ever supplies the *contents* that header
`#include`s in place of its own built-in backend, and only once the
matching `_CUSTOM` macro opts out of that built-in backend in the first
place.
