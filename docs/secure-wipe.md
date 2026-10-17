<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Secure wipe: `secure_wipe` and `secure_vector<T>`

Keys, passwords and plaintext must not stay in memory after their last use. The usual fix, a `memset` before the buffer is freed, is a
dead store: nothing reads the memory afterwards, so the optimizer removes it and the secret stays behind. reloco has one primitive
that is not removed, and a container built on it.

| Header | Provides |
|--------|----------|
| `include/reloco/secure_wipe.hpp` | `secure_wipe(void *, std::size_t)`, `secure_wipe(T &)`, and the `RELOCO_SECURE_WIPE_IMPL` port hook |
| `include/reloco/secure_vector.hpp` | `secure_vector<T>`: a `vector<T>`-like buffer of trivial elements that wipes whatever it releases |

`secure_vector<T>` is also the C++ meaning of kif's `secure_vector<T>` (kif `docs/kif-secure-wipe.md`, rule W12), so the two behave alike.

## 1. `secure_wipe`

```cpp
#include <reloco/secure_wipe.hpp>

struct session_key {
  std::uint8_t bytes[32];
  std::uint64_t counter;
};

void use(session_key &key) {
  // ... derive and use the key ...

  // The object is dead after this call: never read it back. A trivially copyable object (or array
  // of them) is wiped as a whole through `unsigned char`, which both C and C++ allow for any object.
  reloco::secure_wipe(key);
}

void use_buffer(std::uint8_t *buf, std::size_t n) {
  // The raw form: `buf` must point to `n` live, writable bytes that are not a `const` object.
  // It is RELOCO_UNSAFE_BUFFER_USAGE, so call it from a RELOCO_BEGIN_UNSAFE_BUFFER_USAGE region.
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  reloco::secure_wipe(buf, n);   // a null pointer or n == 0 is a no-op
  RELOCO_END_UNSAFE_BUFFER_USAGE
}
```

Rules:

* **Live, writable, non-const storage only.** Writing to storage whose lifetime ended, or to an object defined `const`, is undefined
  behaviour. The object overload `static_assert`s on `const T`.
* **Trivially copyable only** (object overload). Overwriting the bytes of a non-trivially-copyable C++ object is undefined behaviour,
  and could leak what it owns. Wipe such a type's plain members, or hold it in a type that wipes itself.
* **Wiped objects are dead.** The primitive may write something other than zeros; nothing may depend on the bytes afterwards.
* **It erases that storage only.** Copies the program or the compiler made elsewhere (a by-value argument, a register, a spill slot) are
  not touched. Keep secrets in few, short-lived objects.

### What the bytes become, and how the write survives

The default writes zeros and keeps the write:

| Compiler | Default |
|----------|---------|
| GCC, Clang | `std::memset(p, 0, n)` followed by an empty `asm volatile` that takes `p` as an input and clobbers memory: the compiler must assume the zeroed bytes are observed, so the `memset` cannot be dropped (it may still be fused or vectorised) |
| others | a loop of stores through a `volatile unsigned char *` |

Replace it for a port by defining `RELOCO_SECURE_WIPE_IMPL(ptr, bytes)` **before the first reloco include**, and the same way in every
translation unit (the macro changes an inline function, so mixing definitions is an ODR violation):

```cpp
// A FreeBSD kernel port: explicit_bzero(9) is never elided, so no barrier is needed.
// `ptr` is a `void *` and `bytes` a `std::size_t`; the replacement must not be removable by the compiler.
#include <sys/systm.h>
#define RELOCO_SECURE_WIPE_IMPL(ptr, bytes) explicit_bzero((ptr), (bytes))

#include <reloco/secure_vector.hpp>
```

```cpp
// A device whose SRAM imprints long-held values (burn-in): destroy with the complement, then random data,
// so cells do not keep one stable value. `board_sram_scrub` lives in a separately compiled file, so the
// compiler cannot prove the writes dead.
void board_sram_scrub(void *ptr, std::size_t bytes) noexcept;
#define RELOCO_SECURE_WIPE_IMPL(ptr, bytes) board_sram_scrub((ptr), (bytes))
```

The contract of the replacement is only that the previous contents are destroyed and the call cannot be elided. A hardware crypto engine,
a TRNG fill or a cryptographic erase are all valid.

## 2. `secure_vector<T>`

`secure_vector<T>` is a separate type from `vector<T>`, which is unchanged. Its elements must be **trivially copyable and trivially
destructible** (`static_assert`): they are copied bytewise and never destroyed, which is what makes a bytewise wipe valid. It is for
temporary buffers: key schedules, plaintext, scratch blocks. An owning or non-trivial value belongs in a struct that wipes itself, holding
the `secure_vector` as a member.

```cpp
#include <reloco/secure_vector.hpp>

reloco::result<void> decrypt(reloco::allocator_ref alloc) {
  // Fallible creation with room for 64 bytes; `alloc` is any reloco allocator (the default heap one here).
  auto plain = reloco::secure_vector<std::uint8_t>::try_allocate(alloc, 64);
  if (!plain)
    return reloco::unexpected(plain.error());

  // Growing never reallocates in place: it copies into a new block and WIPES the old one before freeing it.
  for (std::uint8_t b : read_block())
    if (auto r = plain->try_push_back(b); !r)   // `b` is passed by value and wiped inside the call
      return r;

  consume(*plain);   // checked access: operator[], front(), back(), data(), iteration

  plain->pop_back(); // the vacated byte is wiped
  plain->clear();    // the live bytes are wiped, the allocation is kept
  return {};
}                    // the destructor wipes ALL capacity() elements (spare capacity too), then frees
```

### What is wiped, and when

| Operation | Wiped |
|-----------|-------|
| `try_reserve`, `try_push_back`, `try_insert_at`, `try_resize` that must grow, `try_shrink_to_fit` | the **whole old block**, after the contents were copied to the new one (never `reallocate` / `expand_in_place`) |
| growth that fails half way | the partial copy in the new block |
| `try_pop_back`, `pop_back` | the vacated element |
| `clear` | all live elements (allocation kept) |
| `try_erase_at` | the vacated tail slot (the shift would otherwise leave a stale duplicate) |
| `try_resize` smaller | the removed elements |
| `wipe()` | the whole capacity, then size 0 (allocation kept) |
| destructor, move assignment (the replaced buffer) | **all `capacity()` elements**, then free |
| a by-value `T` argument (`try_push_back`, `try_insert_at`, `try_resize(count, value)`) | the copy made inside the call |

Not wiped, on purpose or by limit:

* The caller's own copy of a value it passes in, and anything read out (`v[i]` copied to a local).
* Move construction and a moved-from vector: nothing is copied and nothing is freed; the new owner wipes it.
* A reference or iterator into the buffer is invalid after any growth (as for `vector`); the wipe erases the old block it pointed into.

### Access, safety tiers and lifetimes

`secure_vector` follows [Container contract](container-contract.md) and [Hardened containers](hardened-containers.md):

| Tier | Members |
|------|---------|
| checked (asserts, also under `NDEBUG`) | `operator[]`, `front`, `back`, `data`, `pop_back` |
| fallible (`reloco::result`) | `try_at`, `try_front`, `try_back`, `try_data`, `try_push_back`, `try_pop_back`, `try_insert_at`, `try_erase_at`, `try_resize`, `try_reserve`, `try_shrink_to_fit`, `try_clone` |
| unsafe (`RELOCO_UNSAFE_BUFFER_USAGE`, debug-only checks) | `unsafe_at`, `unsafe_data` |

Every accessor that returns a reference, pointer or iterator is `RELOCO_LIFETIMEBOUND` and has its rvalue form deleted
(`RELOCO_BLOCK_RVALUE_ACCESS`), so `make_secret().data()` does not compile. The class is `RELOCO_OWNER`.

### Moving data in and out

```cpp
// Deep copies are explicit: the clone is a secure_vector too, with its own wipe-on-release.
auto copy = plain->try_clone();

// There is NO implicit conversion between vector<T> and secure_vector<T>, in either direction. The one way out
// is explicit and greppable, and it gives up the guarantee: the returned plain vector frees its buffer unwiped.
reloco::vector<std::uint8_t> plain_vector = std::move(*plain).unwrap_unwiped();
```

`secure_vector` composes with `collection_view`/`container_ref` (`collection_view_traits`, `container_ref_traits`) like `vector`, and
is `is_trivially_relocatable`.

## 3. Integrating with a secure struct

A struct that holds a `secure_vector` needs no extra code for the buffer: the member's destructor wipes and frees it. Wipe the plain
fields yourself in the destructor body (members are destroyed after the body, so the fields are erased first):

```cpp
struct session {
  std::uint8_t key[32];                       // plain secret data: wiped by hand below
  reloco::secure_vector<std::uint8_t> ticket; // wiped and freed by its own destructor
  std::uint32_t owner_id;                     // not secret: deliberately left alone

  ~session() { reloco::secure_wipe(key); }    // key is a trivially copyable array
};
```

When kif's C++ backend lowers a `#[secure_wipe]` struct it generates exactly this shape (kif `docs/kif-secure-wipe.md`, section 10a).

## 4. Limits

* **Registers and spill slots.** The compiler may copy a secret into a register or a stack slot reloco never sees. Use the compiler's
  options (`-fzero-call-used-regs`, stack clearing) and keep secrets in few, short-lived objects.
* **Copies you made.** The wipe covers the container's own bytes.
* **Crashes.** A path that never reaches the destructor (abort, a fatal signal) does not wipe.
* **Hardware.** Caches, store buffers and DRAM remanence are out of reach of software; the `RELOCO_SECURE_WIPE_IMPL` hook is where a port
  adds a hardware-specific erase.
* **Timing.** A wipe is a fixed-size write; it does not make surrounding code constant time.
* **Elements are plain data.** `secure_vector<std::string>` does not compile, by design.
