// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-narrowing-flow: explicit integer casts silence reloco-implicit-conversion, so they get a second look here.
// A small value-range analysis (constants, masks, modulo, shifts, small types, arithmetic on known ranges) tries to prove
// the value fits the destination; casts that are not provably lossless are reported.

#include "Checks.h"

#include "clang/AST/ParentMapContext.h"

#include <algorithm>
#include <optional>
#include <string>

namespace reloco::tidy {

namespace {

using i128 = __int128;
struct Range {
  i128 lo, hi;
};

bool isInt(QualType t) { return !t.isNull() && !t->isDependentType() && t->isIntegralOrEnumerationType(); }

Range typeRange(QualType t, const ASTContext &ctx) {
  const unsigned w = ctx.getIntWidth(t);
  if (t->isBooleanType())
    return {0, 1};
  if (t->isSignedIntegerOrEnumerationType())
    return {-(static_cast<i128>(1) << (w - 1)), (static_cast<i128>(1) << (w - 1)) - 1};
  return {0, (static_cast<i128>(1) << w) - 1};
}

bool fits(Range r, Range d) { return r.lo >= d.lo && r.hi <= d.hi; }

Range clampTo(Range r, Range d) { return fits(r, d) ? r : d; }

std::optional<i128> constant(const Expr *e, const ASTContext &ctx) {
  Expr::EvalResult res;
  if (e->isValueDependent() || !e->EvaluateAsInt(res, ctx))
    return std::nullopt;
  const llvm::APSInt &v = res.Val.getInt();
  return v.isSigned() ? static_cast<i128>(v.getSExtValue()) : static_cast<i128>(v.getZExtValue());
}

Range rangeOf(const Expr *e, const ASTContext &ctx, int depth = 0) {
  const QualType t = e->getType();
  const Range full = typeRange(t, ctx);
  if (depth > 8)
    return full;
  e = e->IgnoreParens();
  if (auto c = constant(e, ctx))
    return {*c, *c};
  if (const auto *cast = llvm::dyn_cast<CastExpr>(e); cast && isInt(cast->getSubExpr()->getType()) &&
                                                        (cast->getCastKind() == CK_IntegralCast || cast->getCastKind() == CK_NoOp ||
                                                         cast->getCastKind() == CK_LValueToRValue)) {
    // The value survives the cast only if it fits the destination type; otherwise the cast result is unknown.
    const Range inner = rangeOf(cast->getSubExpr(), ctx, depth + 1);
    return fits(inner, full) ? inner : full;
  }
  if (const auto *bo = llvm::dyn_cast<BinaryOperator>(e); bo && isInt(bo->getLHS()->getType()) && isInt(bo->getRHS()->getType())) {
    const Range l = rangeOf(bo->getLHS(), ctx, depth + 1);
    const Range r = rangeOf(bo->getRHS(), ctx, depth + 1);
    switch (bo->getOpcode()) {
    case BO_And:
      if (l.lo >= 0 && r.lo >= 0)
        return clampTo({0, std::min(l.hi, r.hi)}, full);
      if (r.lo >= 0)
        return clampTo({0, r.hi}, full);
      if (l.lo >= 0)
        return clampTo({0, l.hi}, full);
      break;
    case BO_Rem:
      if (r.lo > 0)
        return clampTo({l.lo >= 0 ? 0 : -(r.hi - 1), r.hi - 1}, full);
      break;
    case BO_Shr:
      if (r.lo >= 0 && r.hi < 64 && l.lo >= 0)
        return clampTo({l.lo >> static_cast<int>(r.hi), l.hi >> static_cast<int>(r.lo)}, full);
      break;
    case BO_Div:
      if (r.lo > 0 && l.lo >= 0)
        return clampTo({l.lo / r.hi, l.hi / r.lo}, full);
      break;
    case BO_Add:
      return clampTo({l.lo + r.lo, l.hi + r.hi}, full);
    case BO_Sub:
      return clampTo({l.lo - r.hi, l.hi - r.lo}, full);
    case BO_Mul: {
      const i128 c[] = {l.lo * r.lo, l.lo * r.hi, l.hi * r.lo, l.hi * r.hi};
      return clampTo({*std::min_element(c, c + 4), *std::max_element(c, c + 4)}, full);
    }
    default:
      break;
    }
  }
  if (const auto *co = llvm::dyn_cast<ConditionalOperator>(e); co && isInt(co->getTrueExpr()->getType()) &&
                                                                 isInt(co->getFalseExpr()->getType())) {
    const Range a = rangeOf(co->getTrueExpr(), ctx, depth + 1);
    const Range b = rangeOf(co->getFalseExpr(), ctx, depth + 1);
    return clampTo({std::min(a.lo, b.lo), std::max(a.hi, b.hi)}, full);
  }
  return full;
}

std::string str(i128 v) {
  if (v < 0)
    return "-" + str(-v);
  std::string s;
  do {
    s.insert(s.begin(), static_cast<char>('0' + static_cast<int>(v % 10)));
    v /= 10;
  } while (v);
  return s;
}

bool isCharType(QualType t) { return t->isAnyCharacterType() || t->isSpecificBuiltinType(BuiltinType::SChar); }

} // namespace

void NarrowingFlowCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(explicitCastExpr(unless(isInTemplateInstantiation()), hasSourceExpression(expr())).bind("cast"), this);
}

void NarrowingFlowCheck::check(const MatchFinder::MatchResult &result) {
  const auto *ce = result.Nodes.getNodeAs<ExplicitCastExpr>("cast");
  ASTContext &ctx = *result.Context;
  const Expr *sub = ce->getSubExprAsWritten();
  const QualType dst = ce->getTypeAsWritten();
  const QualType src = sub->getType();
  if (!isInt(dst) || !isInt(src) || dst->isBooleanType() || ctx.getIntWidth(dst) > 64 || ctx.getIntWidth(src) > 64 ||
      ce->isValueDependent() || exempt(*result.SourceManager, ce->getBeginLoc()))
    return;
  // Reinterpreting characters as bytes is the idiomatic, same-width sign change.
  if (isCharType(src) && ctx.getIntWidth(src) == ctx.getIntWidth(dst))
    return;

  const Range dr = typeRange(dst, ctx);
  const Range sr = rangeOf(sub, ctx);
  if (fits(sr, dr))
    return;

  diag(ce->getBeginLoc(), "explicit cast from %0 to %1 may truncate or change the sign: value range [%2, %3] does not fit "
                          "[%4, %5]")
      << src << dst << str(sr.lo) << str(sr.hi) << str(dr.lo) << str(dr.hi);
  howToFix(ce->getBeginLoc(), "use 'reloco::checked_cast<To>(x)' and handle the result, 'saturating' conversion when "
                              "clamping is intended, or prove the range to the checker with a mask or modulo "
                              "('static_cast<uint8_t>(x & 0xFF)'); suppress with NOLINT only for intentional wrapping");
}

// ---------------------------------------------------------------------------
// reloco-signed-index
// ---------------------------------------------------------------------------
void SignedIndexCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(arraySubscriptExpr(unless(isInTemplateInstantiation())).bind("sub"), this);
  finder->addMatcher(cxxOperatorCallExpr(hasOverloadedOperatorName("[]"), unless(isInTemplateInstantiation())).bind("op"), this);
  finder->addMatcher(cxxMemberCallExpr(callee(cxxMethodDecl(hasAnyName("at", "try_at"))), unless(isInTemplateInstantiation()))
                         .bind("at"),
                     this);
}

void SignedIndexCheck::check(const MatchFinder::MatchResult &result) {
  const Expr *index = nullptr;
  const Expr *whole = nullptr;
  if (const auto *as = result.Nodes.getNodeAs<ArraySubscriptExpr>("sub")) {
    index = as->getIdx();
    whole = as;
  } else if (const auto *oc = result.Nodes.getNodeAs<CXXOperatorCallExpr>("op"); oc && oc->getNumArgs() == 2) {
    index = oc->getArg(1);
    whole = oc;
  } else if (const auto *mc = result.Nodes.getNodeAs<CXXMemberCallExpr>("at"); mc && mc->getNumArgs() >= 1) {
    index = mc->getArg(0);
    whole = mc;
  }
  if (!index || index->isValueDependent() || exempt(*result.SourceManager, whole->getBeginLoc()))
    return;
  // The conversion to size_t hides the signedness; look at the index as written.
  const Expr *written = index->IgnoreParenImpCasts();
  const QualType t = written->getType();
  if (!isInt(t) || t->isBooleanType() || t->isEnumeralType() || !t->isSignedIntegerType() ||
      result.Context->getIntWidth(t) > 64)
    return;
  const Range r = rangeOf(written, *result.Context);
  if (r.lo >= 0)
    return;
  diag(written->getBeginLoc(), "index of signed type %0 may be negative (range [%1, %2]); a negative index wraps to a huge "
                               "unsigned value")
      << t << str(r.lo) << str(r.hi);
  howToFix(written->getBeginLoc(), "make the index an unsigned type ('std::size_t i'), iterate with a range-for, or check "
                                   "'i >= 0' first and use the checked accessor ('try_at'); "
                                   "'static_cast<std::size_t>(i)' only moves the problem to reloco-narrowing-flow");
}

} // namespace reloco::tidy
