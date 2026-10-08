# LLDB pretty printers

`reloco` ships LLDB data formatters for the same containers, views, and smart
pointers covered by the [GDB pretty printers](gdb-pretty-printers.md), so
`frame variable`/`p` show `reloco::vector of length 3, capacity 8` plus its
elements instead of raw `data_`/`size_`/`cap_` fields. The summary strings
match the GDB printers, and children use the same names (`value`, `error`,
`get()`, `[i]`).

Everything lives in `tools/lldb/reloco_printers.py`.

## Loading

In a running session:

```
(lldb) command script import /path/to/reloco/tools/lldb/reloco_printers.py
```

Permanently, by adding the same line to `~/.lldbinit` (or to a project-local
`.lldbinit`, which LLDB only reads when `target.load-cwd-lldbinit` is on).

Importing the module runs `__lldb_init_module`, which registers every
formatter in the `reloco` type category and enables it. To switch it off:
`type category disable reloco`.

Unlike the GDB printers there is no embedded-in-the-binary mode: LLDB has no
portable equivalent of GDB's `.debug_gdb_scripts` section.

## Coverage

Same set of types as the GDB printers (see
[Coverage](gdb-pretty-printers.md#coverage)). `collection_view` and
`container_ref` are not covered, for the same reason: their size is only
reachable by calling through a vtable function pointer in the target.

## Testing

`tools/lldb/testbed/run.sh` builds the shared
`tools/gdb/testbed/testbed.cpp` with Clang (`CXX`, default `clang++-24`),
stops at the `GDB_BREAK` marker, runs `frame variable` for every
`// GDB_CHECK:` case under LLDB (`LLDB`, default `lldb-24`), and verifies the
expected substring. Adding a case to the testbed therefore covers both
debuggers. Like the GDB testbed, it is not part of the main build or CI;
run it explicitly after editing `reloco_printers.py`.
