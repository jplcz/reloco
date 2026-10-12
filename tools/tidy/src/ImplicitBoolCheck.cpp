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
  const UnaryOperator *notOp = nullptr;
  for (const auto &p : result.Context->getParents(*ce))
    if (const auto *uo = p.get<UnaryOperator>(); uo && uo->getOpcode() == UO_LNot)
      notOp = uo;
  const bool simple = end.isValid() && !end.isMacroID() && !sub->getEndLoc().isMacroID() &&
                      llvm::isa<DeclRefExpr, MemberExpr, CallExpr>(sub->IgnoreParenImpCasts());
  // `if (T *p = f())`: the operand is the condition variable itself, spelled at its declaration; no rewrite is possible.
  const auto *dre = llvm::dyn_cast<DeclRefExpr>(sub->IgnoreParenImpCasts());
  if (dre && dre->getDecl()->getLocation() == dre->getLocation())
    return;
  if (!simple)
    return;
  if (!notOp) {
    d << FixItHint::CreateInsertion(end, std::string(" ") + cmp);
    return;
  }
  // `!p` becomes `p == nullptr`; parenthesised unless the parent is a logical operator or a condition, where the
  // lower precedence of `==` cannot change the meaning.
  if (notOp->getBeginLoc().isMacroID() || notOp->getBeginLoc().isInvalid())
    return;
  bool bare = false;
  for (const auto &p : result.Context->getParents(*notOp)) {
    if (const auto *bo = p.get<BinaryOperator>(); bo && bo->isLogicalOp())
      bare = true;
    else if (p.get<IfStmt>() || p.get<WhileStmt>() || p.get<ReturnStmt>() || p.get<ForStmt>())
      bare = true;
    else if (const auto *pe = p.get<ParenExpr>(); pe && !pe->getBeginLoc().isMacroID())
      bare = true;
  }
  const StringRef text = Lexer::getSourceText(CharSourceRange::getTokenRange(sub->getSourceRange()), sm,
                                              result.Context->getLangOpts());
  std::string eq = text.str();
  eq += t->isPointerType() || t->isMemberPointerType() ? " == nullptr" : t->isFloatingType() ? " == 0.0" : " == 0";
  if (!bare)
    eq = "(" + eq + ")";
  d << FixItHint::CreateReplacement(CharSourceRange::getCharRange(notOp->getBeginLoc(), end), eq);
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
  std::string list; // the explicit replacement for the default capture
  bool canFix = true;
  bool hasExplicit = false;
  for (const LambdaCapture &c : le->captures()) {
    if (!c.isImplicit()) {
      hasExplicit = true;
      continue;
    }
    if (c.capturesThis()) {
      if (!names.empty()) { names += ", "; list += ", "; }
      names += "this";
      list += "this";
      continue;
    }
    if (!c.capturesVariable()) {
      canFix = false;
      continue;
    }
    const std::string n = c.getCapturedVar()->getName().str();
    if (n.empty())
      canFix = false;
    if (!names.empty()) { names += ", "; list += ", "; }
    names += n;
    list += (byRef && c.getCaptureKind() == LCK_ByRef ? "&" : "") + n;
  }
  auto d = diag(le->getCaptureDefaultLoc(), "lambda uses the '%select{=|&}0' default capture, which hides what it captures%1")
           << byRef << (names.empty() ? std::string() : "; implicitly captured: " + names);
  const SourceManager &sm = *result.SourceManager;
  const SourceLocation defLoc = le->getCaptureDefaultLoc();
  if (canFix && !defLoc.isMacroID()) {
    SourceLocation end = Lexer::getLocForEndOfToken(defLoc, 0, sm, result.Context->getLangOpts());
    if (hasExplicit) {
      // `[&, x]`: drop the default together with its comma.
      if (const auto next = Lexer::findNextToken(defLoc, sm, result.Context->getLangOpts()); next && next->is(tok::comma)) {
        end = Lexer::getLocForEndOfToken(next->getLocation(), 0, sm, result.Context->getLangOpts());
        while (*sm.getCharacterData(end) == ' ')
          end = end.getLocWithOffset(1);
      } else
        canFix = false;
      if (!list.empty())
        list += ", ";
    }
    if (canFix)
      d << FixItHint::CreateReplacement(CharSourceRange::getCharRange(defLoc, end), list);
  }
  howToFix(defLoc, "list every capture explicitly ('[x, &y]'), and use an init-capture "
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
