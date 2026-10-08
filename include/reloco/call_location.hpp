// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file call_location.hpp
 * @brief `reloco::call_location` + `reloco::call_location_ref`
 * (aliased `debug_call_location_ref`) + `reloco::release_call_location_ref`:
 * an optional, compile-time-stripable `file:line` caller location, for
 * passing into assert/fault-reporting-style functions without forcing
 * every build (or every call site) to pay for it.
 *
 * `call_location` is the plain `{file, line}` pair. `call_location_ref`
 * is the thin (two words: a pointer plus an `int`), trivially-copyable
 * wrapper actually passed around *by value* -- a default-constructed
 * ref (or `call_location_ref::none()`) means "no location is
 * available"; `call_location_ref::current()` captures the location of
 * its own caller.
 *
 * ## Why `current()` instead of a `__FILE__`/`__LINE__` macro?
 *
 * The obvious-looking alternative -- a macro expanding to
 * `call_location{__FILE__, __LINE__}` -- silently breaks the moment it
 * is used as a *default argument*: default-argument expressions are
 * bound once, at the point a function is *declared*, so
 * `void fault(const char *msg, call_location_ref where = SOME_MACRO())`
 * would always report `call_location.hpp`'s own declaration site, never
 * the caller's. `current()` instead uses the GCC/Clang
 * `__builtin_FILE()`/`__builtin_LINE()` intrinsics (the same mechanism
 * `std::source_location::current()` is built on) as its own parameters'
 * default values; these intrinsics are specially re-evaluated at each
 * omitted-argument call site, including transitively through a chain of
 * default arguments -- so `current()` correctly reports whichever real
 * call site ultimately triggered it, however many default-argument
 * hops away that call site is. This requires a GCC- or Clang-family
 * compiler (checked at compile time via `__has_builtin`); see
 * `RELOCO_CALL_LOCATION_HAS_BUILTINS` below for the fallback story on
 * a compiler without them.
 *
 * ## Why not just pass `const char *file, int line` (or `call_location`
 * by value/reference) directly, instead of through `call_location_ref`?
 *
 * One uniform currency for "maybe a location". Every location-aware API
 * in `reloco` takes exactly one `call_location_ref` parameter rather
 * than inventing its own optional-pair convention (an extra
 * `bool has_location` flag, a sentinel `line == 0`, overloading on
 * argument count, ...) -- whether a given call site can supply a real
 * location or not, the parameter type never changes.
 *
 * ## Worked example: call sites that architecturally cannot supply a
 * location at all
 *
 * Some call sites -- a fixed-signature function pointer slot (an
 * `extern "C"` callback table entry, a trap handler vector, anything
 * crossing an ABI boundary the kernel does not control the shape of) --
 * cannot grow an extra parameter just because one caller happens to
 * know its own source location. For exactly these cases, a
 * location-aware kernel API still ships a location-*less* export
 * alongside the location-aware one, both forwarding into one shared
 * implementation:
 *
 * @code
 * void fault_impl(const char *message, reloco::call_location_ref where) {
 *   if (where.has_value()) {
 *     const reloco::call_location loc = where.value();
 *     // ... report message, loc.file, loc.line ...
 *   } else {
 *     // ... report message with no location available ...
 *   }
 * }
 *
 * // Ordinary call sites: location captured automatically.
 * inline void fault(const char *message,
 *                    reloco::call_location_ref where = reloco::call_location_ref::current()) {
 *   fault_impl(message, where);
 * }
 *
 * // extern "C" callback-table slot: fixed signature, cannot carry a
 * // caller location, so the kernel exports this fallback that forwards
 * // "no location" into the exact same implementation.
 * extern "C" void fault_no_location(const char *message) {
 *   fault_impl(message, reloco::call_location_ref::none());
 * }
 * @endcode
 *
 * ## `debug_call_location_ref` vs. `release_call_location_ref`: picking
 * an ABI contract, not just a default value
 *
 * `call_location_ref` (aliased `debug_call_location_ref` for symmetry
 * with the name below) always has the same two-word layout, but *what*
 * it defaults to when a caller omits it still depends on
 * `RELOCO_ENABLE_CALL_LOCATION`/`RELOCO_CALL_LOCATION_HAS_BUILTINS` --
 * fine for code that only ever links against itself, but not something
 * an inter-module ABI boundary (a stable shared-library export, a
 * versioned driver/plugin interface) should depend on: two translation
 * units built with different settings must still agree on the exported
 * function's behavior. `release_call_location_ref` is the answer for
 * that boundary -- a permanently empty type (no data members at all,
 * `has_value()` is unconditionally `false`, nothing to ever `value()`)
 * that cannot vary with any build setting, now or if `call_location`
 * ever grows new fields.
 *
 * A caller-facing API that wants both -- full location capture for
 * ordinary, same-DSO/statically-linked callers, and a permanently
 * frozen shape for the stable export -- ships one overload per type.
 * Exactly one of the two overloads gets a default argument -- which one
 * is selected build-wide by `RELOCO_CALL_LOCATION_DEBUG` (see
 * `reloco_config.hpp`), never both at once, so an omitted-argument
 * call site (`mutex_instance.lock()` below) is never ambiguous between
 * the two overloads:
 *
 * @code
 * class mutex {
 * public:
 *   // Debug builds (RELOCO_CALL_LOCATION_DEBUG != 0): the default
 *   // argument lives here, so an omitted-argument call resolves to
 *   // this overload and captures a real location.
 *   void lock(reloco::debug_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG) {
 *     lock_impl(where);
 *   }
 *
 *   // Release builds (RELOCO_CALL_LOCATION_DEBUG == 0): the default
 *   // argument lives here instead, so an omitted-argument call resolves
 *   // to this overload -- always zero-sized, frozen shape.
 *   void lock(reloco::release_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE) {
 *     (void)where;
 *     lock_impl(reloco::call_location_ref::none());
 *   }
 *
 * private:
 *   void lock_impl(reloco::call_location_ref where);
 * };
 * @endcode
 */

#include <reloco/reloco_config.hpp>

// RELOCO_CALL_LOCATION_HAS_BUILTINS
//     1 when __builtin_FILE()/__builtin_LINE() are available (GCC/Clang
//     family), in which case call_location_ref::current() correctly
//     forwards its caller's real location through any number of
//     default-argument hops (see this header's @file block). 0 on a
//     compiler without them, in which case current() falls back to
//     plain __FILE__/__LINE__ -- still correct when current() is called
//     directly/explicitly at the real call site, but it reports
//     call_location.hpp's own declaration site (not the real caller)
//     when relied upon as some *other* function's default argument, the
//     same limitation a naive __FILE__/__LINE__ macro would have.
// MSVC has no __has_builtin, but ships __builtin_FILE()/__builtin_LINE()
// since Visual Studio 2019 16.6 (_MSC_VER 1926).
#if (defined(__has_builtin) && __has_builtin(__builtin_FILE) && __has_builtin(__builtin_LINE)) ||                      \
    (defined(_MSC_VER) && !defined(__clang__) && _MSC_VER >= 1926)
#define RELOCO_CALL_LOCATION_HAS_BUILTINS 1
#else
#define RELOCO_CALL_LOCATION_HAS_BUILTINS 0
#endif

#if !defined(RELOCO_ENABLE_CALL_LOCATION)
#define RELOCO_ENABLE_CALL_LOCATION 1
#endif

#if RELOCO_ENABLE_CALL_LOCATION
#if RELOCO_CALL_LOCATION_HAS_BUILTINS
#define RELOCO_DETAIL_CALL_LOCATION_FILE_DEFAULT __builtin_FILE()
#define RELOCO_DETAIL_CALL_LOCATION_LINE_DEFAULT __builtin_LINE()
#else
#define RELOCO_DETAIL_CALL_LOCATION_FILE_DEFAULT __FILE__
#define RELOCO_DETAIL_CALL_LOCATION_LINE_DEFAULT __LINE__
#endif
#else
#define RELOCO_DETAIL_CALL_LOCATION_FILE_DEFAULT nullptr
#define RELOCO_DETAIL_CALL_LOCATION_LINE_DEFAULT 0
#endif

namespace reloco {

/**
 * @brief A caller's source location: the `file`/`line` pair at some
 * call site. Plain data, no invariants to guard -- see
 * `call_location_ref` for the nullable wrapper actually passed around.
 */
struct call_location {
  /** @brief Source file path at the captured call site. Never null when obtained from a `call_location_ref` whose
     `has_value()` is true. */
  const char *file;
  /** @brief Source line number at the captured call site. */
  int line;
};

/**
 * @brief Thin (pointer plus `int`), trivially-copyable "maybe a caller
 * location" wrapper -- the uniform type every location-aware `reloco`
 * API takes by value. Construct one via `current()` (capture the
 * immediate caller's own location) or `none()`/the default constructor
 * (no location), and read it back via `has_value()`/`value()`.
 */
class call_location_ref {
public:
  /** @brief Wraps "no location is available". Same as `none()`. */
  constexpr call_location_ref() noexcept : file_(nullptr), line_(0) {}

  /** @brief Wraps the given `location`'s `file`/`line` by value. @p location.file may be null, in which case this
     behaves exactly like `none()` regardless of @p location.line. */
  constexpr call_location_ref(const call_location &location) noexcept : file_(location.file), line_(location.line) {}

  /** @brief Explicit "no location is available", for call sites that prefer a named factory over the default
     constructor (e.g. `call_location_ref::none()` passed as an explicit argument). */
  static constexpr call_location_ref none() noexcept { return call_location_ref(); }

  /**
   * @brief Captures the immediate caller's own `file`/`line`. Correctly
   * forwards the real call site through any number of default-argument
   * hops when `RELOCO_CALL_LOCATION_HAS_BUILTINS` is 1 (see this
   * header's @file block); only correct for a direct/explicit call
   * otherwise. Returns `none()` when `RELOCO_ENABLE_CALL_LOCATION` is
   * 0 (see `reloco_config.hpp`).
   */
  static constexpr call_location_ref
  current(const char *file = RELOCO_DETAIL_CALL_LOCATION_FILE_DEFAULT,
          int line = RELOCO_DETAIL_CALL_LOCATION_LINE_DEFAULT) noexcept {
    return call_location_ref(call_location{file, line});
  }

  /** @brief Whether this `call_location_ref` actually wraps a location. */
  constexpr bool has_value() const noexcept { return file_ != nullptr; }

  /** @brief The wrapped `call_location`, by value. Only call when `has_value()` is true. */
  constexpr call_location value() const noexcept { return call_location{file_, line_}; }

private:
  const char *file_;
  int line_;
};

/**
 * @brief Alias for `call_location_ref`, used to name the pairing with
 * `release_call_location_ref` at an API that exposes both (see this
 * header's @file block's `mutex::lock` example) -- not a distinct type,
 * just a documentation-facing name for "the variant that may actually
 * carry a location".
 */
using debug_call_location_ref = call_location_ref;

/**
 * @brief Permanently empty companion to `call_location_ref`/
 * `debug_call_location_ref`, for an inter-module ABI boundary (a stable
 * shared-library export, a versioned driver/plugin interface) that must
 * never change shape depending on `RELOCO_ENABLE_CALL_LOCATION`,
 * `RELOCO_CALL_LOCATION_HAS_BUILTINS`, or any field `call_location`
 * ever grows in the future. Carries no data at all: `has_value()` is
 * unconditionally `false`, and there is no `value()` -- nothing is ever
 * stored to return. See this header's @file block for the paired
 * `debug_call_location_ref`/`release_call_location_ref` overload
 * pattern.
 */
class release_call_location_ref {
public:
  /** @brief The only state this type can be in. Same as `none()`/`current()`. */
  constexpr release_call_location_ref() noexcept = default;

  /** @brief Returns the (only) empty instance, for call sites that prefer a named factory over the default
     constructor. */
  static constexpr release_call_location_ref none() noexcept { return release_call_location_ref(); }

  /** @brief Returns the (only) empty instance. Named to mirror `call_location_ref::current()` for use as a
     default-argument initializer at a release-mode/ABI-stable API; never actually captures anything. */
  static constexpr release_call_location_ref current() noexcept { return release_call_location_ref(); }

  /** @brief Always `false` -- this type never carries a location. */
  constexpr bool has_value() const noexcept { return false; }
};

} // namespace reloco

// RELOCO_CALL_LOCATION_DEBUG
//     Selects which of RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG/
//     RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE (below) actually carries
//     a default argument -- 1 (the default) picks the debug one, 0
//     picks the release one. A dedicated reloco switch, deliberately
//     *not* NDEBUG/assert-disabled or any other host-toolchain
//     convention: whether the paired debug_call_location_ref/
//     release_call_location_ref overload pattern (see this header's
//     @file block) defaults to the debug or the release side is a
//     distinct decision from whatever the surrounding build's own
//     debug/release flavor is.
#if !defined(RELOCO_CALL_LOCATION_DEBUG)
#define RELOCO_CALL_LOCATION_DEBUG 1
#endif

// RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG / RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE
//     A complementary pair of default-argument clauses (each already
//     includes its own leading "="): write whichever one matches a
//     parameter's type directly after the parameter name, with no "="
//     of your own -- e.g.
//     `reloco::debug_call_location_ref where RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG`.
//     Selected by RELOCO_CALL_LOCATION_DEBUG, exactly one of the pair
//     is ever non-empty at a time: the other expands to nothing, so
//     that parameter has no default and its overload is not a
//     candidate for an omitted-argument call. This is what keeps the
//     paired debug_call_location_ref/release_call_location_ref overload
//     pattern (see this header's @file block) from ever being
//     ambiguous -- if both overloads had a default argument
//     simultaneously, an omitted-argument call site would not compile.
#if RELOCO_CALL_LOCATION_DEBUG
#define RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG = ::reloco::debug_call_location_ref::current()
#define RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE
#else
#define RELOCO_CALL_LOCATION_DEFAULT_IF_DEBUG
#define RELOCO_CALL_LOCATION_DEFAULT_IF_RELEASE = ::reloco::release_call_location_ref::current()
#endif
