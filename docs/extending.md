<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Extending reloco: the context_type-based provider pattern

Adapted from [microfmt's extending guide](https://github.com/jplcz/microfmt/blob/master/docs/extending.md),
this is reloco's own compile-time customization pattern for pluggable
backends (allocators, and any future provider abstraction): an empty tag
selects a `*_traits<Tag>` specialization that declares a `context_type` and
static operations; a typed wrapper or caller-owned object holds the
`context_type` by value; and a type-erased `*_ref` borrows it for use in
non-templated code. No virtual interfaces, no allocation, and no runtime
registration are involved anywhere in this pattern.

`reloco::allocator_ref`/`reloco::allocator_traits`/`reloco::allocator`
(`include/reloco/allocator.hpp`) is the first, fully worked implementation of
this pattern in reloco; `include/reloco/heap_allocator.hpp` is a concrete
stateless backend (`heap_allocator_tag`) built against it. Read both
alongside this page as a complete, real example.

`allocator_ref`'s every operational method (`allocate`, `deallocate`,
`expand_in_place`, `reallocate`, `advise`) is marked `RELOCO_UNSAFE_BUFFER_USAGE`
and must be called from inside a `RELOCO_BEGIN_UNSAFE_BUFFER_USAGE`/
`RELOCO_END_UNSAFE_BUFFER_USAGE` block (see `docs/lifetime-safety.md`),
since raw memory management has no bounds-tracked alternative to opt into
instead — unlike span/array, where only the `unsafe_*`-named subset carries
that attribute. `can_expand_in_place()`/`can_reallocate()`/`can_advise()`/
`operator bool()` are ordinary checked accessors and are exempt. Apply the
same convention to any new provider whose operations manage raw memory or
otherwise have no safer alternative.

There are two different tasks this pattern covers, with two different
templates below:

* **1. Plug into an existing provider abstraction** — specialize an
  *already-defined* `*_traits<Tag>` (e.g. `reloco::allocator_traits`) for
  your own backend. This is by far the more common case and requires no
  vtable code at all; reloco already defines the `*_ref` type and its
  vtable.
* **2. Define a brand-new provider abstraction** — you are adding a new kind
  of pluggable customization point to your own code (not one of reloco's
  existing ones), so you need the tag, the traits primary template, the
  type-erased `*_ref` class, *and* its vtable derivation. Use this template
  when 1 does not apply because no matching `*_traits` exists yet.

## 1. Specialize an existing provider

```cpp
// 1. An empty tag identifies your implementation.
struct your_allocator_tag {};

// 2. State your provider needs, held by value (no virtual base, no vtable).
struct your_allocator_context {
  // ... whatever your operations need to read and mutate ...
};

// 3. Specialize the relevant `*_traits<Tag>` in namespace reloco.
template <> struct reloco::allocator_traits<your_allocator_tag> {
  using context_type = your_allocator_context;

  // Mutating operation: value_ref<context_type>.
  static reloco::alloc_result<reloco::mem_block>
  allocate(reloco::value_ref<context_type> context, std::size_t bytes,
          std::size_t alignment) noexcept {
    // ... implement using context->... ...
  }

  static void deallocate(reloco::value_ref<context_type> context, void *ptr,
                        std::size_t bytes) noexcept {
    // ...
  }
};

// 4a. Borrow a caller-owned context directly (useful when it is shared);
//     the `*_ref` type is constructed from the tag and a context reference:
your_allocator_context state{/* ... */};
reloco::allocator_ref provider_ref{your_allocator_tag{}, state};

// 4b. Or use a typed wrapper that owns context_type by value, when the
//     state naturally belongs to the provider itself, then call .ref():
reloco::allocator<your_allocator_tag> owned{your_allocator_context{/* ... */}};
auto ref_from_owned = owned.ref();
```

Notes:

* Use `reloco::value_ref<const context_type>` for read-only operations and
  `reloco::value_ref<context_type>` for operations that mutate state (e.g. a
  bump-pointer allocator advancing its offset). Optional state in an
  already-erased handle uses `value_ptr<const T>`/`value_ptr<T>` instead.
* For a stateless provider, declare `using context_type = void;` and omit
  the context parameter from every operation (see
  `include/reloco/heap_allocator.hpp`'s `heap_allocator_tag`).
* The context (or its owning wrapper) must outlive every `*_ref`/view
  derived from it — type erasure never transfers ownership.
* Optional operations on a trait are detected at compile time (SFINAE); omit
  an operation entirely rather than providing a stub that reports failure,
  so callers can distinguish "unsupported" (`can_reallocate()` etc.
  returning `false`) from "failed this time" (the operation itself
  returning an error).

## 2. Define a new provider abstraction (full vtable derivation)

This is the complete, self-contained template for creating a new
`*_ref`-style type-erased handle from scratch: a tag, a traits primary
template, a two-word handle (context pointer + vtable pointer) with a
mandatory operation and an optional operation, and an owning wrapper.
`reloco::allocator_ref` is built from this same skeleton — copy it, rename
the placeholders, and add/remove operations as needed.

```cpp
#include <reloco/lifetime.hpp> // RELOCO_LIFETIMEBOUND
#include <reloco/value_ref.hpp>
#include <type_traits>
#include <utility>

// 1. An empty tag identifies a concrete implementation.
struct your_provider_tag {};

// 2. Primary template, intentionally left undefined. Each backend provides
//    a specialization (see step 6) with a `context_type` and static
//    operations; instantiating the primary template is a compile error,
//    which is the desired "you forgot to specialize this" diagnostic.
template <typename Tag> struct your_provider_traits;

namespace detail {

// 3. Compile-time detection for an *optional* trait operation ("write").
//    Required operations (like "read") need no detection: just call them
//    directly from the vtable trampoline in step 4 and let a missing
//    specialization fail to compile with a clear error.
template <typename Tag, typename = void>
struct has_your_provider_write : std::false_type {};

template <typename Tag>
struct has_your_provider_write<
    Tag, std::void_t<decltype(your_provider_traits<Tag>::write(
             std::declval<reloco::value_ref<
                 typename your_provider_traits<Tag>::context_type>>(),
             std::declval<int>(), std::declval<int>()))>> : std::true_type {};

} // namespace detail

// 4. The type-erased handle: one context pointer plus one vtable pointer.
//    No virtual base class, no RTTI, no allocation -- `s_vtbl<Tag>` below
//    is a distinct static object per Tag, and its address acts as a
//    lightweight, per-Tag "type id" the handle carries around.
class your_provider_ref {
public:
  struct vtable {
    bool (*read)(const void *ctx, int key, int &out_value) noexcept;
    // `write` is nullptr for backends whose traits omit it (read-only).
    bool (*write)(void *ctx, int key, int value) noexcept;
  };

  constexpr your_provider_ref() noexcept = default;

  // Binds the handle to a caller-owned context. `Context` must be (or
  // derive from) the Tag's declared `context_type`.
  template <typename Tag, typename Context,
            typename Traits = your_provider_traits<Tag>,
            std::enable_if_t<std::is_convertible_v<
                                 Context *, typename Traits::context_type *>,
                             int> = 0>
  constexpr your_provider_ref(Tag, Context &ctx RELOCO_LIFETIMEBOUND) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  [[nodiscard]] bool read(int key, int &out_value) const noexcept {
    return vtbl_ && vtbl_->read(ctx_, key, out_value);
  }

  // Optional operations are exposed via a `can_*`/operation pair so callers
  // can distinguish "unsupported" from "failed this time".
  [[nodiscard]] bool can_write() const noexcept { return vtbl_ && vtbl_->write; }

  bool write(int key, int value) const noexcept {
    return vtbl_ && vtbl_->write &&
           vtbl_->write(const_cast<void *>(ctx_), key, value);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  // Trampolines recover the concrete `context_type` from the erased `void*`
  // and forward to the Traits specialization selected by `Tag`. One
  // trampoline instantiation exists per Tag the handle is ever bound to.
  template <typename Tag>
  static bool read_entry(const void *ctx, int key, int &out_value) noexcept {
    using context_type = typename your_provider_traits<Tag>::context_type;
    const auto &typed = *static_cast<const context_type *>(ctx);
    return your_provider_traits<Tag>::read(
        reloco::value_ref<const context_type>(typed), key, out_value);
  }

  // Optional-operation trampolines are themselves selected at compile time:
  // `if constexpr` picks between a real trampoline and a null function
  // pointer, so unsupported operations cost nothing and are detectable via
  // `vtbl_->write == nullptr` (exposed above as `can_write()`).
  template <typename Tag>
  static constexpr auto write_entry() noexcept {
    using context_type = typename your_provider_traits<Tag>::context_type;
    if constexpr (detail::has_your_provider_write<Tag>::value) {
      return +[](void *ctx, int key, int value) noexcept {
        auto &typed = *static_cast<context_type *>(ctx);
        return your_provider_traits<Tag>::write(
            reloco::value_ref<context_type>(typed), key, value);
      };
    } else {
      return static_cast<bool (*)(void *, int, int) noexcept>(nullptr);
    }
  }

  // One `constexpr` vtable instance per Tag, built once at compile time and
  // stored in `.rodata` -- this *is* the "vtable derivation": deriving a
  // concrete function-pointer table from whatever `Tag`'s Traits
  // specialization provides, with no runtime registration step.
  template <typename Tag>
  static constexpr vtable s_vtbl{&read_entry<Tag>, write_entry<Tag>()};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// 5. Optional owning wrapper: holds `context_type` by value so the context
//    and the handle share a single object's lifetime.
template <typename Tag> class your_provider {
public:
  using traits_type = your_provider_traits<Tag>;
  using context_type = typename traits_type::context_type;

  constexpr explicit your_provider(context_type context) noexcept
      : context_(std::move(context)) {}

  // Ref-qualified `&`: calling `.ref()` on a temporary owning wrapper is a
  // compile error, since the returned handle would otherwise outlive the
  // `context_` it points into.
  [[nodiscard]] constexpr your_provider_ref ref() & noexcept RELOCO_LIFETIMEBOUND {
    return your_provider_ref(Tag{}, context_);
  }

private:
  context_type context_;
};

// 6. A concrete backend: specialize the traits primary template from step 2.
struct your_provider_context {
  int storage[16]{};
};

template <> struct your_provider_traits<your_provider_tag> {
  using context_type = your_provider_context;

  static bool read(reloco::value_ref<const context_type> ctx, int key,
                    int &out_value) noexcept {
    if (key < 0 || key >= 16)
      return false;
    out_value = ctx->storage[key];
    return true;
  }

  // Omit entirely (do not stub it out) for a read-only backend; `can_write()`
  // will then report `false` for handles bound to this Tag.
  static bool write(reloco::value_ref<context_type> ctx, int key,
                    int value) noexcept {
    if (key < 0 || key >= 16)
      return false;
    ctx->storage[key] = value;
    return true;
  }
};

// --- Usage ---
your_provider_context state{};
your_provider_ref ref{your_provider_tag{}, state};
int value = 0;
if (ref.read(3, value)) { /* ... */ }
if (ref.can_write())
  ref.write(3, 99);

your_provider<your_provider_tag> owned{your_provider_context{}};
auto owned_ref = owned.ref();
```

Adapting this template:

* Add one `<operation>_entry`/`vtable` field pair per operation; keep
  mandatory operations un-detected (a missing specialization should fail to
  compile) and gate every optional operation behind a `has_your_provider_*`
  trait-detection struct, following the `write` example.
* If some backends have no per-instance data, give them `context_type = void`
  traits instead of an empty struct — see 2c below for the ref-side changes
  this requires.
* Keep every vtable function pointer `noexcept`; the handle's own public
  methods should be `noexcept` as well so failures are reported through
  return values, not exceptions.

## 2c. Stateless traits (`context_type = void`)

Some backends have no per-instance data at all — every call is answered
purely from global/static state (the process heap, a fixed hardware
register bank, a compile-time-known table, ...). For these, declare
`using context_type = void;` and drop the `value_ref<...>` parameter from
every operation entirely, rather than specializing traits with an empty
placeholder struct. `reloco::heap_allocator_tag` in
`include/reloco/heap_allocator.hpp` is a real example of this shape.

```cpp
// A stateless backend: no context object, so every operation is a plain
// static function with no context parameter at all.
struct your_stateless_provider_tag {};

template <> struct your_provider_traits<your_stateless_provider_tag> {
  using context_type = void;

  static bool read(int key, int &out_value) noexcept {
    // ... answer purely from global/static state, no `ctx` parameter ...
    out_value = key * 2;
    return true;
  }
};
```

The `*_ref` handle from step 2 needs two changes to support both stateful
and stateless tags side by side: a second constructor overload taking only
the tag (no context argument), enabled via `std::is_void_v<...>`; and an
`if constexpr` branch inside each entry trampoline that skips the
`value_ref` wrapping entirely for stateless tags. Both branches populate the
same `s_vtbl<Tag>`, so callers use `read()`/`write()` identically regardless
of which kind of tag they were bound to:

```cpp
class your_provider_ref {
public:
  struct vtable {
    bool (*read)(const void *ctx, int key, int &out_value) noexcept;
  };

  constexpr your_provider_ref() noexcept = default;

  // Stateless tag: no context object required.
  template <typename Tag, typename Traits = your_provider_traits<Tag>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit your_provider_ref(Tag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  // Stateful tag: bind to a caller-owned context (same as step 2).
  template <typename Tag, typename Context,
            typename Traits = your_provider_traits<Tag>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type> &&
                                 std::is_convertible_v<
                                     Context *, typename Traits::context_type *>,
                             int> = 0>
  constexpr your_provider_ref(Tag, Context &ctx RELOCO_LIFETIMEBOUND) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  [[nodiscard]] bool read(int key, int &out_value) const noexcept {
    return vtbl_ && vtbl_->read(ctx_, key, out_value);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

private:
  template <typename Tag>
  static bool read_entry(const void *ctx, int key, int &out_value) noexcept {
    using context_type = typename your_provider_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      // Stateless: `ctx` is always nullptr here, ignore it and call the
      // traits operation directly.
      (void)ctx;
      return your_provider_traits<Tag>::read(key, out_value);
    } else {
      const auto &typed = *static_cast<const context_type *>(ctx);
      return your_provider_traits<Tag>::read(
          reloco::value_ref<const context_type>(typed), key, out_value);
    }
  }

  template <typename Tag>
  static constexpr vtable s_vtbl{&read_entry<Tag>};

  const void *ctx_{nullptr};
  const vtable *vtbl_{nullptr};
};

// --- Usage: no context object anywhere. ---
your_provider_ref stateless_ref{your_stateless_provider_tag{}};
int value = 0;
if (stateless_ref.read(21, value)) { /* value == 42 */ }
```

Notes:

* An *optional* stateless operation needs its own SFINAE-detection struct,
  separate from the stateful one, since its expected signature has no
  `value_ref<...>` parameter to detect against (mirror
  `detail::has_stateless_allocator_expand_in_place` alongside
  `detail::has_allocator_expand_in_place` in `include/reloco/allocator.hpp`).
* Do not default-construct an empty `context_type` struct just to keep a
  single code path; `void` is the correct signal both to the compiler (no
  storage, no pointer dereference) and to a reader of the traits
  specialization (this backend genuinely has no per-instance state).
* A tag's traits must pick one shape per operation (stateless or stateful,
  not both); the two constructor overloads above are already mutually
  exclusive on `std::is_void_v<typename Traits::context_type>`, so a given
  `Tag` can only ever match one of them.

## 3. Wiring a provider as the process-wide default

Once a Tag has working `*_traits<Tag>`, some providers also want a single
process-wide instance that library-internal call sites reach for when the
caller does not pass an explicit `*_ref`, similar to Rust's
`#[global_allocator]`. `reloco::default_allocator()`
(`include/reloco/default_allocator.hpp`) is reloco's example of this: it
forwards to a plain hook struct rather than calling a Tag directly, so the
hook can be forward declared and overridden without ever including
`allocator.hpp` from `reloco_user_config.hpp`:

```cpp
namespace reloco {
struct reloco_global_alloc {
  [[nodiscard]] static allocator_ref default_allocator() noexcept;
};

[[nodiscard]] inline allocator_ref default_allocator() noexcept {
  return reloco_global_alloc::default_allocator();
}
} // namespace reloco
```

`include/reloco/default_allocator.hpp` supplies the built-in definition of
`reloco_global_alloc::default_allocator()` (`allocator<heap_allocator_tag>::
ref()`, the process heap) unless the application defines
`RELOCO_DEFAULT_ALLOCATOR_CUSTOM`, in which case it must supply exactly one
out-of-line definition of that static member itself.

This two-piece shape — a forward-declarable hook struct plus an
opt-out-only macro — exists because of a real ordering hazard:
`reloco_user_config.hpp` is reached from `detail/compat.hpp`'s very first
line, before that header has even defined its own feature-detection macros
(`RELOCO_HAS_ATTRIBUTE` and friends). Any reloco header pulled in from
`reloco_user_config.hpp` would re-enter `compat.hpp` while it is still on
the include stack; `#pragma once` would then skip that re-entry and leave
those macros undefined, breaking every downstream header (see
`reloco_config.hpp` for the full explanation). So `reloco_user_config.hpp`
may only ever contain `#define`s — never `#include <reloco/...>` — and the
actual `reloco_global_alloc::default_allocator()` override, along with the
Tag and `allocator_traits<Tag>` specialization it depends on, must live in
its own ordinary header that the application includes through its normal
path instead:

```cpp
// reloco_user_config.hpp -- #defines only, no #include of any reloco header.
#define RELOCO_DEFAULT_ALLOCATOR_CUSTOM

// my_arena_allocator.hpp -- included normally elsewhere by the app, e.g.
// from a source file, before any use of reloco::default_allocator().
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>

struct my_arena_tag {};

template <> struct reloco::allocator_traits<my_arena_tag> {
  using context_type = my_arena;
  // ... allocate/deallocate, per section 1 above ...
};

inline reloco::allocator_ref reloco::reloco_global_alloc::default_allocator() noexcept {
  static my_arena arena;
  return reloco::allocator<my_arena_tag>(arena).ref();
}
```

Apply the same shape to any new process-wide default you add: a forward
declarable hook struct with one static member, a built-in definition guarded
by `#if !defined(RELOCO_YOUR_THING_CUSTOM)`, and documentation directing
overriders to define the opt-out macro in `reloco_user_config.hpp` while
placing the real override in its own header.
