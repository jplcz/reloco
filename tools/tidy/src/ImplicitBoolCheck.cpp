// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/Lex/Lexer.h"

namespace reloco::tidy {

void ImplicitBoolCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(
      implicitCastExpr(anyOf(hasCastKind(CK_PointerToBoolean), hasCastKind(CK_IntegralToBoolean),
                             hasCastKind(CK_FloatingToBoolean), hasCastKind(CK_MemberPointerToBoolean)),
                       unless(hasAncestor(staticAssertDecl())), unless(isInTemplateInstantiation()))
          .bind("cast"),
      this);
}

void ImplicitBoolCheck::check(const MatchFinder::MatchResult &result) {
  const auto *ce = result.Nodes.getNodeAs<ImplicitCastExpr>("cast");
  const Expr *sub = ce->getSubExpr();
  if (!ce || ce->getBeginLoc().isMacroID() || exempt(*result.SourceManager, ce->getBeginLoc()) ||
      sub->isTypeDependent() || sub->getType()->isBooleanType())
    return;
  // `if (flag)` where the source is a bool-valued constant expression (e.g. `true` spelled as 1 in a macro) is noise.
  bool constant = false;
  if (!sub->isValueDependent() && sub->EvaluateAsBooleanCondition(constant, *result.Context) && llvm::isa<IntegerLiteral>(sub->IgnoreParenImpCasts()))
    return;
  const QualType t = sub->getType();
  const char *cmp = t->isPointerType() || t->isMemberPointerType() ? "!= nullptr" : t->isFloatingType() ? "!= 0.0" : "!= 0";
  auto d = diag(sub->getBeginLoc(), "%0 is implicitly converted to bool; compare explicitly") << t;
  const SourceManager &sm = *result.SourceManager;
  const SourceLocation end = Lexer::getLocForEndOfToken(sub->getEndLoc(), 0, sm, result.Context->getLangOpts());
  // Only a simple operand is rewritten; a compound expression would need parentheses.
  // `!p` is rewritten by hand (`p == nullptr`): appending a comparison would bind to the negation.
  bool negated = false;
  for (const auto &p : result.Context->getParents(*ce))
    if (const auto *uo = p.get<UnaryOperator>(); uo && uo->getOpcode() == UO_LNot)
      negated = true;
  if (!negated && end.isValid() && !end.isMacroID() && !sub->getEndLoc().isMacroID() &&
      llvm::isa<DeclRefExpr, MemberExpr, CallExpr>(sub->IgnoreParenImpCasts()))
    d << FixItHint::CreateInsertion(end, std::string(" ") + cmp);
}

} // namespace reloco::tidy

namespace reloco::tidy {

// reloco-lambda-default-capture
void LambdaDefaultCaptureCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(lambdaExpr(unless(isInTemplateInstantiation())).bind("lambda"), this);
}

void LambdaDefaultCaptureCheck::check(const MatchFinder::MatchResult &result) {
  const auto *le = result.Nodes.getNodeAs<LambdaExpr>("lambda");
  if (!le || le->getCaptureDefault() == LCD_None || exempt(*result.SourceManager, le->getBeginLoc()) ||
      le->getBeginLoc().isMacroID())
    return;
  const bool byRef = le->getCaptureDefault() == LCD_ByRef;
  std::string names;
  for (const LambdaCapture &c : le->captures()) {
    if (!c.isImplicit())
      continue;
    if (!names.empty())
      names += ", ";
    names += c.capturesThis() ? "this" : c.getCapturedVar()->getName().str();
  }
  diag(le->getCaptureDefaultLoc(), "lambda uses the '%select{=|&}0' default capture, which hides what it captures%1")
      << byRef << (names.empty() ? std::string() : "; implicitly captured: " + names);
  howToFix(le->getCaptureDefaultLoc(), "list every capture explicitly ('[x, &y]'), and use an init-capture "
                                       "('[v = std::move(v)]') to move; capture '*this' by copy instead of 'this' "
                                       "when the lambda may outlive the object");
}

} // namespace reloco::tidy

namespace reloco::tidy {

// reloco-move-const: std::move of a const object yields `const T&&`, which binds to the copy constructor.
void MoveConstCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(callExpr(callee(functionDecl(hasAnyName("::std::move", "::reloco::move"))), argumentCountIs(1),
                              unless(isInTemplateInstantiation()))
                         .bind("move"),
                     this);
}

void MoveConstCheck::check(const MatchFinder::MatchResult &result) {
  const auto *call = result.Nodes.getNodeAs<CallExpr>("move");
  const Expr *arg = call->getArg(0)->IgnoreParenImpCasts();
  const QualType t = arg->getType();
  if (call->getBeginLoc().isMacroID() || exempt(*result.SourceManager, call->getBeginLoc()) || t.isNull() ||
      t->isDependentType() || !t.isConstQualified() || t->isReferenceType())
    return;
  // Trivially copyable values copy either way; nothing is lost by the move.
  if (t.isTriviallyCopyableType(*result.Context))
    return;
  diag(call->getBeginLoc(), "std::move of a const %0 does not move: it selects the copy constructor") << t.getUnqualifiedType();
  howToFix(call->getBeginLoc(), "drop the 'const' from the object so it can be moved from, or remove the std::move "
                                "and write the copy explicitly");
}

} // namespace reloco::tidy
