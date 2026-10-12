// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "RelocoCheckBase.h"

namespace reloco::tidy {

#define RELOCO_TIDY_CHECK(Name)                                                                                        \
  class Name : public RelocoCheck {                                                                                    \
  public:                                                                                                              \
    using RelocoCheck::RelocoCheck;                                                                                    \
    void registerMatchers(MatchFinder *finder) override;                                                               \
    void check(const MatchFinder::MatchResult &result) override;                                                       \
    bool isLanguageVersionSupported(const LangOptions &lo) const override { return lo.CPlusPlus; }                     \
  }

// Non-static members must be &/const&-qualified (or have an explicit &&
// overload) so they cannot be called on temporaries.
RELOCO_TIDY_CHECK(RefQualifiedMemberCheck);
// Raw pointers only inside gsl::pointer/gsl::owner types, the unsafe tier,
// vtables, function pointers and extern "C"; everything else uses value_ptr/value_ref/span.
RELOCO_TIDY_CHECK(NoRawPointerCheck);
// Every function (and lambda) must be noexcept.
RELOCO_TIDY_CHECK(NoexceptCheck);
// Classes are move-only unless marked RELOCO_COPYABLE (Rust's Copy): implicit copies are flagged,
// explicit copies T(x) / T y(x) / static_cast<T>(x) (Rust's .clone()) are allowed.
RELOCO_TIDY_CHECK(ImplicitCopyCheck);
// Aliasing accessors need [[clang::lifetimebound]]; handle constructors need
// lifetimebound or lifetime_capture_by on borrowed parameters.
RELOCO_TIDY_CHECK(LifetimeAnnotationsCheck);
// Classes that hold borrowed storage must be gsl::pointer, owners gsl::owner.
RELOCO_TIDY_CHECK(PointerAnnotationCheck);
// std:: facilities that have a reloco/microfmt/structo equivalent are banned.
RELOCO_TIDY_CHECK(BannedStdTypeCheck);
// throw/try/catch, new/delete, const_cast, reinterpret_cast, C-style casts, C string/memory functions.
RELOCO_TIDY_CHECK(BannedConstructCheck);

// Best-effort Rust-style borrow checking inside one function: a live borrow of a place excludes
// conflicting access to it (shared borrows exclude mutation, mutable borrows exclude every use).
class BorrowCheck : public RelocoCheck {
public:
  using RelocoCheck::RelocoCheck;
  void registerMatchers(MatchFinder *finder) override;
  void check(const MatchFinder::MatchResult &result) override;
  bool isLanguageVersionSupported(const LangOptions &lo) const override { return lo.CPlusPlus; }
  bool honorsUnsafeBufferUsage() const override { return true; }
};

// Parameters marked RELOCO_OUT must be written on every path that returns, and not read before.
RELOCO_TIDY_CHECK(OutParamCheck);
// Locals must be initialised at declaration (no indeterminate scalars/arrays/trivial aggregates).
RELOCO_TIDY_CHECK(UninitLocalCheck);

// Raw C arrays (variables, members) are banned in favour of reloco::span/array/inline_vector.
RELOCO_TIDY_CHECK(NoCArrayCheck);

// Use of a local/parameter after std::move(x) without reassigning it (Rust's "borrow of moved value").
class UseAfterMoveCheck : public RelocoCheck {
public:
  using RelocoCheck::RelocoCheck;
  void registerMatchers(MatchFinder *finder) override;
  void check(const MatchFinder::MatchResult &result) override;
  bool isLanguageVersionSupported(const LangOptions &lo) const override { return lo.CPlusPlus; }

private:
  llvm::DenseSet<const FunctionDecl *> seen_;
};

// Structural modification of a container inside a range-for over it (hidden iterators invalidated).
RELOCO_TIDY_CHECK(IteratorInvalidationCheck);

// RELOCO_MUST_CONSUME values (linear resources) must be consumed on every path (Rust's explicit drop / must-use values).
RELOCO_TIDY_CHECK(LeakCheck);

// Members of a class that owns a mutex must be RELOCO_GUARDED_BY it (or RELOCO_UNGUARDED / const / atomic).
RELOCO_TIDY_CHECK(LockGuardCheck);

// Explicit integer casts must be provably lossless by value-range analysis (masks, modulo, shifts, constants).
RELOCO_TIDY_CHECK(NarrowingFlowCheck);

// Arguments to RELOCO_REQUIRE_SEND / RELOCO_REQUIRE_SYNC parameters (thread/task spawn points) must be Send / Sync.
RELOCO_TIDY_CHECK(SendSyncCheck);

// Indexing ([] and at) with a signed type whose value range may be negative.
RELOCO_TIDY_CHECK(SignedIndexCheck);

// Pointers tagged RELOCO_NS(tag) (foreign address spaces) cannot be dereferenced, mixed or cast outside a
// RELOCO_NS_TRANSLATOR function.
RELOCO_TIDY_CHECK(NsPointerCheck);

// RELOCO_EARLY_STATIC types need constexpr construction and trivial destruction, and their static objects must be
// constant-initialised (bare-metal startup, before the C++ runtime runs).
RELOCO_TIDY_CHECK(EarlyStaticCheck);

// Implicit narrowing / sign-changing / lossy float conversions need an explicit cast.
RELOCO_TIDY_CHECK(ImplicitConversionCheck);
// Single-argument constructors and conversion operators must be explicit.
RELOCO_TIDY_CHECK(ExplicitConversionCheck);
// Functions returning expected/result/error/optional/status types must be [[nodiscard]].
RELOCO_TIDY_CHECK(MustUseResultCheck);
// A discarded call result of such a type is an error.
RELOCO_TIDY_CHECK(UnusedResultCheck);
// Locals that are never mutated must be const.
RELOCO_TIDY_CHECK(ConstLocalCheck);
// A local must not shadow another local or parameter.
RELOCO_TIDY_CHECK(NoShadowCheck);
// Enum switches are exhaustive and have no default.
RELOCO_TIDY_CHECK(EnumSwitchCheck);
// goto, setjmp/longjmp, unions, volatile.
RELOCO_TIDY_CHECK(BannedLowLevelCheck);
// Mutable namespace-scope/static variables.
RELOCO_TIDY_CHECK(MutableGlobalCheck);
// operator[], unary operator* and operator-> must be RELOCO_CHECKED, and only checked ones may be used.
RELOCO_TIDY_CHECK(CheckedOperatorCheck);
// Inside RELOCO_SAFE_MATH code, raw integer arithmetic is rejected.
RELOCO_TIDY_CHECK(SafeMathCheck);
// Every field has a default member initializer.
RELOCO_TIDY_CHECK(MemberInitCheck);
// Declaring any special member means declaring all five.
RELOCO_TIDY_CHECK(RuleOfFiveCheck);

// Calls to unsafe-tier functions (unsafe_*, [[unsafe_buffer_usage]]) are only allowed from the unsafe tier
// or inside unsafe regions (including Clang's unsafe_buffer_usage blocks).
class UnsafeCallScopeCheck : public RelocoCheck {
public:
  using RelocoCheck::RelocoCheck;
  void registerMatchers(MatchFinder *finder) override;
  void check(const MatchFinder::MatchResult &result) override;
  bool isLanguageVersionSupported(const LangOptions &lo) const override { return lo.CPlusPlus; }
  bool honorsUnsafeBufferUsage() const override { return true; }
};

#undef RELOCO_TIDY_CHECK

} // namespace reloco::tidy
