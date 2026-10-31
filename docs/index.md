---
layout: home
title: reloco
---
<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

<img src="{{ '/assets/logo/reloco-logo-small.png' | relative_url }}" alt="reloco logo" width="200">

**reloco** ("**Rel**iable **Co**mponents") is a header-only C++17 library of
*Safety by Default* replacements for Standard Library containers and value-lifetime
primitives. Anything that can fail — an out-of-bounds index, a use of a moved-from value, a
stale pointer — is rejected at compile time or returned as an explicit `result<T>` instead of
undefined behavior or an exception. It is meant for firmware, RTOS and interrupt code, crash
handlers and kernel-adjacent code.

Source and issue tracker: [github.com/jplcz/reloco](https://github.com/jplcz/reloco).

## Where to start

New here? Read in this order: the *Hardened containers* guide explains the three-tier
(checked / `try_` / `unsafe_`) access convention everything else follows, *Lifetime safety*
explains borrowed values, and *Fallible construction* explains how allocation failure is
reported. Then pick the container or primitive you need.

### Safety model
- [Hardened containers](hardened-containers.md)
- [Lifetime safety](lifetime-safety.md)
- [Fallible construction](fallible-construction.md)
- [Safety checker (`reloco-tidy`)](safety-checker.md)
- [Container contract](container-contract.md)
- [Extending reloco](extending.md)

### Containers
- [Type-erased base containers](type-erased-base-containers.md)
- [Deque containers](deque-containers.md)
- [Ring buffers](ring-buffer.md) and [lock-free SPSC ring buffers](atomic-ring-buffer.md)
- [Balanced containers](balanced-containers.md), [tree containers](tree-containers.md),
  [flat hash containers](flat-hash-containers.md)
- [Linked containers](linked-containers.md)
- [Span slice operations](span-slice-ops.md), [string operations](string-ops.md),
  [string view operations](string-view-ops.md), [tuple](tuple.md)

### Pointers, ownership and memory
- [Unique pointer](unique-ptr.md), [intrusive pointers](intrusive-ptr.md)
- [Trivial relocation](relocatable.md), [alignment](alignment.md)
- [Secure wipe](secure-wipe.md)
- [Allocator capacity absorption](allocator-capacity-absorption.md)

### Concurrency
- [Send/Sync](send-sync.md), [lock annotations](lock-annotations.md),
  [lock discipline](lock-discipline.md)
- [Atomic context](atomic-context.md), [futex backend](futex.md),
  [per-CPU slots](per-cpu-slot.md), [TLS slots](tls-slot.md)
- [Fault injection](fault-injection.md), [coroutines](coroutine.md)

### Binary data and utilities
- [Byte cursors](byte-cursor.md), [endian](endian.md), [call location](call_location.md)
- [Reflection support](reflection.md)

### Kernel framework (C++20+)
- [Kernel framework overview](kernel/README.md)

### Tooling and integration
- [Package managers](package-managers.md), [shared libraries](shared-library.md)
- [GDB pretty printers](gdb-pretty-printers.md), [LLDB pretty printers](lldb-pretty-printers.md)

### Reference
- [API reference](reference.md)
