// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Analysis/Analyses/ExprMutationAnalyzer.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringSet.h"

namespace reloco::tidy {

namespace {

bool hasAnnotation(const Decl *d, llvm::StringRef name) {
  for (const Decl *r : d->redecls())
    for (const auto *a : r->specific_attrs<AnnotateAttr>())
      if (a->getAnnotation() == name)
        return true;
  return false;
}

// The marker on the declaration itself or on any enclosing class/function.
bool annotatedInScope(const Decl *d, llvm::StringRef name) {
  for (; d && !llvm::isa<TranslationUnitDecl>(d); d = Decl::castFromDeclContext(d->getDeclContext()))
    if (hasAnnotation(d, name))
      return true;
  return false;
}

const FunctionDecl *enclosingFunction(const MatchFinder::MatchResult &r, const char *id = "encl") {
  return r.Nodes.getNodeAs<FunctionDecl>(id);
}

bool isFallibleName(const CXXRecordDecl *rd) {
  static const llvm::StringSet<> names = {"expected", "result", "error", "status", "optional", "try_result"};
  return rd && rd->getIdentifier() && names.contains(rd->getName());
}

const CXXRecordDecl *recordOf(QualType t) { return t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl(); }

} // namespace

// ---------------------------------------------------------------------------
// reloco-implicit-conversion
// ---------------------------------------------------------------------------
void ImplicitConversionCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(implicitCastExpr(anyOf(hasCastKind(CK_IntegralCast), hasCastKind(CK_FloatingToIntegral),
                                            hasCastKind(CK_FloatingCast), hasCastKind(CK_IntegralToFloating)),
                                      unless(isInTemplateInstantiation()), unless(isExpansionInSystemHeader()))
                         .bind("cast"),
                     this);
}

void ImplicitConversionCheck::check(const MatchFinder::MatchResult &result) {
  const auto *ce = result.Nodes.getNodeAs<ImplicitCastExpr>("cast");
  ASTContext &ctx = *result.Context;
  const QualType dst = ce->getType();
  const QualType src = ce->getSubExpr()->getType();
  if (dst.isNull() || src.isNull() || dst->isDependentType() || src->isDependentType() || ce->isValueDependent() ||
      exempt(*result.SourceManager, ce->getBeginLoc()))
    return;
  if (dst->isBooleanType() || src->isBooleanType() || src->isEnumeralType())
    return;
  // static_cast<T>(x) and T(x) carry their conversion as an implicit child: that is the explicit form.
  for (const DynTypedNode &p : ctx.getParents(*ce))
    if (p.get<ExplicitCastExpr>())
      return;

  bool lossy = false;
  switch (ce->getCastKind()) {
  case CK_FloatingToIntegral:
    lossy = true;
    break;
  case CK_FloatingCast:
    lossy = llvm::APFloat::semanticsPrecision(ctx.getFloatTypeSemantics(dst)) <
            llvm::APFloat::semanticsPrecision(ctx.getFloatTypeSemantics(src));
    break;
  case CK_IntegralToFloating:
    lossy = llvm::APFloat::semanticsPrecision(ctx.getFloatTypeSemantics(dst)) < ctx.getIntWidth(src) -
                                                                                    (src->isSignedIntegerType() ? 1 : 0);
    break;
  case CK_IntegralCast: {
    if (!dst->isIntegerType() || !src->isIntegerType())
      return;
    const unsigned dw = ctx.getIntWidth(dst);
    const unsigned sw = ctx.getIntWidth(src);
    const bool ds = dst->isSignedIntegerType();
    const bool ss = src->isSignedIntegerType();
    lossy = dw < sw || (ss && !ds) || (!ss && ds && dw <= sw);
    break;
  }
  default:
    return;
  }
  if (!lossy)
    return;
  // `x & mask` with a non-negative constant mask that fits the destination is bounded by the mask (the nibble idiom).
  if (const auto *bo = llvm::dyn_cast<BinaryOperator>(ce->getSubExpr()->IgnoreParenImpCasts());
      bo && bo->getOpcode() == BO_And && dst->isIntegerType()) {
    for (const Expr *side : {bo->getLHS(), bo->getRHS()}) {
      Expr::EvalResult mr;
      if (side->EvaluateAsRValue(mr, ctx) && mr.Val.isInt() && mr.Val.getInt().isNonNegative() &&
          mr.Val.getInt().isIntN(ctx.getIntWidth(dst) - (dst->isSignedIntegerType() ? 1 : 0)))
        return;
    }
  }
  // A constant that fits the destination converts losslessly.
  Expr::EvalResult er;
  if (ce->getSubExpr()->EvaluateAsRValue(er, ctx)) {
    if (er.Val.isInt() && dst->isIntegerType()) {
      const llvm::APSInt &v = er.Val.getInt();
      const unsigned dw = ctx.getIntWidth(dst);
      if (dst->isSignedIntegerType() ? v.isSignedIntN(dw) && (v.isSigned() || v.isIntN(dw - 1)) : v.isUnsigned() ? v.isIntN(dw) : (v.isNonNegative() && v.isIntN(dw)))
        return;
    }
    if (er.Val.isFloat() && ce->getCastKind() == CK_FloatingCast)
      return;
  }
  auto d = diag(ce->getBeginLoc(), "implicit conversion from %0 to %1 may change the value; convert explicitly "
                                   "(static_cast or a checked helper)")
           << src << dst;
  const SourceRange sr = ce->getSubExpr()->getSourceRange();
  if (sr.isValid() && !sr.getBegin().isMacroID() && !sr.getEnd().isMacroID()) {
    const SourceLocation end = Lexer::getLocForEndOfToken(sr.getEnd(), 0, *result.SourceManager, ctx.getLangOpts());
    if (end.isValid())
    {
      // A member typedef (e.g. `size_type`) is not nameable at the use site: spell the canonical type instead.
      QualType spelled = dst.getUnqualifiedType();
      if (const auto *tt = spelled->getAs<TypedefType>(); tt && tt->getDecl()->getDeclContext()->isRecord())
        spelled = spelled.getCanonicalType();
      d << FixItHint::CreateInsertion(sr.getBegin(), "static_cast<" + spelled.getAsString(ctx.getPrintingPolicy()) + ">(")
        << FixItHint::CreateInsertion(end, ")");
    }
  }
}

// ---------------------------------------------------------------------------
// reloco-explicit-conversion
// ---------------------------------------------------------------------------
void ExplicitConversionCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxConstructorDecl(unless(isImplicit()), unless(isDeleted()), unless(isExplicit()),
                                        unless(isCopyConstructor()), unless(isMoveConstructor()))
                         .bind("ctor"),
                     this);
  finder->addMatcher(cxxConversionDecl(unless(isImplicit()), unless(isDeleted()), unless(isExplicit())).bind("conv"),
                     this);
}

namespace {
// Location for an `explicit` keyword: after any leading `[[...]]` attribute lists, which must precede decl-specifiers.
SourceLocation explicitInsertionLoc(SourceLocation start, const SourceManager &sm) {
  bool invalid = false;
  const llvm::StringRef buf = sm.getBufferData(sm.getFileID(start), &invalid);
  if (invalid)
    return start;
  size_t off = sm.getFileOffset(start);
  for (;;) {
    if (!buf.substr(off).starts_with("[["))
      break;
    const size_t close = buf.find("]]", off);
    if (close == llvm::StringRef::npos)
      return start;
    off = close + 2;
    while (off < buf.size() && (buf[off] == ' ' || buf[off] == '\t' || buf[off] == '\n'))
      ++off;
  }
  return start.getLocWithOffset(static_cast<int>(off - sm.getFileOffset(start)));
}
} // namespace

void ExplicitConversionCheck::check(const MatchFinder::MatchResult &result) {
  if (const auto *c = result.Nodes.getNodeAs<CXXConstructorDecl>("ctor")) {
    if (c->getNumParams() == 0 || c->getMinRequiredArguments() > 1 || c->getParent()->isLambda() ||
        c->isTemplateInstantiation() || exempt(*result.SourceManager, c->getLocation()))
      return;
    const CXXRecordDecl *pr = recordOf(c->getParamDecl(0)->getType());
    if (pr && pr->getIdentifier() && pr->getName() == "initializer_list")
      return;
    if (c->getParamDecl(0)->getType().getNonReferenceType()->isNullPtrType())
      return; // pointer-like types are conventionally initialised from nullptr: 'T p = nullptr;'
    if (c->isOutOfLine())
      return; // the in-class declaration carries (or lacks) the keyword
    auto d = diag(c->getLocation(), "single-argument constructor of '%0' must be explicit; add 'explicit' (or accept "
                                    "the implicit conversion with NOLINT and a reason)")
             << c->getParent()->getName();
    if (c->getOuterLocStart().isValid() && !c->getOuterLocStart().isMacroID())
      d << FixItHint::CreateInsertion(explicitInsertionLoc(c->getOuterLocStart(), *result.SourceManager), "explicit ");
  } else if (const auto *cv = result.Nodes.getNodeAs<CXXConversionDecl>("conv")) {
    if (cv->isTemplateInstantiation() || cv->getParent()->isLambda() || exempt(*result.SourceManager, cv->getLocation()))
      return;
    if (cv->isOutOfLine())
      return;
    auto d = diag(cv->getLocation(), "conversion operator must be explicit; add 'explicit' (call a named conversion "
                                     "function if an implicit one is really needed)");
    if (cv->getOuterLocStart().isValid() && !cv->getOuterLocStart().isMacroID())
      d << FixItHint::CreateInsertion(explicitInsertionLoc(cv->getOuterLocStart(), *result.SourceManager), "explicit ");
  }
}

// ---------------------------------------------------------------------------
// reloco-must-use-result
// ---------------------------------------------------------------------------
void MustUseResultCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(unless(isImplicit()), unless(returns(voidType())), unless(cxxConstructorDecl()),
                                  unless(cxxDestructorDecl()), unless(isDeleted()))
                         .bind("fn"),
                     this);
}

void MustUseResultCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  // A lambda's call operator cannot portably carry [[nodiscard]] before C++23; its caller consumes the value.
  if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(fn); method && method->getParent()->isLambda())
    return;
  if (fn->getCanonicalDecl() != fn || fn->isTemplateInstantiation() || fn->getReturnType()->isReferenceType() ||
      exempt(*result.SourceManager, fn->getLocation()))
    return;
  const CXXRecordDecl *rd = recordOf(fn->getReturnType());
  if (!isFallibleName(rd) || fn->hasAttr<WarnUnusedResultAttr>() || rd->hasAttr<WarnUnusedResultAttr>())
    return;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
    if (spec->getSpecializedTemplate()->getTemplatedDecl()->hasAttr<WarnUnusedResultAttr>())
      return;
  auto d = diag(fn->getLocation(), "%0 returns a fallible '%1' and must be [[nodiscard]] (or mark the type itself "
                                   "[[nodiscard]] once, so every function returning it is covered)")
           << fn << rd->getName();
  if (fn->getOuterLocStart().isValid() && !fn->getOuterLocStart().isMacroID())
    d << FixItHint::CreateInsertion(fn->getOuterLocStart(), "[[nodiscard]] ");
}

// ---------------------------------------------------------------------------
// reloco-unused-result
// ---------------------------------------------------------------------------
void UnusedResultCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(compoundStmt(forEach(expr().bind("e"))), this);
}

void UnusedResultCheck::check(const MatchFinder::MatchResult &result) {
  const Expr *e = result.Nodes.getNodeAs<Expr>("e");
  while (e) {
    if (const auto *ewc = llvm::dyn_cast<ExprWithCleanups>(e))
      e = ewc->getSubExpr();
    else if (const auto *bt = llvm::dyn_cast<CXXBindTemporaryExpr>(e))
      e = bt->getSubExpr();
    else if (const auto *ic = llvm::dyn_cast<ImplicitCastExpr>(e))
      e = ic->getSubExpr();
    else
      break;
  }
  const auto *call = llvm::dyn_cast_or_null<CallExpr>(e);
  // A call returning a reference (e.g. a defaulted member `operator=`) has an lvalue value category, not a reference type.
  if (!call || call->getType().isNull() || call->getType()->isReferenceType() || call->isGLValue() ||
      exempt(*result.SourceManager, call->getBeginLoc()))
    return;
  if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(call); oc && oc->isAssignmentOp())
    return;
  if (!isFallibleName(recordOf(call->getType())))
    return;
  diag(call->getBeginLoc(), "result of fallible '%0' is discarded; handle it or cast to void with a reason")
      << recordOf(call->getType())->getName();
  howToFix(call->getBeginLoc(), "'if (auto r = f(); !r) return r.error();' to propagate, or '(void)f(); // best effort: <why "
                                "failure is harmless>' to ignore it on purpose");
}

// ---------------------------------------------------------------------------
// reloco-const-local
// ---------------------------------------------------------------------------
void ConstLocalCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(varDecl(hasLocalStorage(), hasInitializer(anything()), unless(parmVarDecl()), unless(isConstexpr()),
                             unless(isImplicit()), hasAncestor(functionDecl(isDefinition()).bind("encl")))
                         .bind("var"),
                     this);
}

void ConstLocalCheck::check(const MatchFinder::MatchResult &result) {
  const auto *vd = result.Nodes.getNodeAs<VarDecl>("var");
  const FunctionDecl *fn = enclosingFunction(result);
  const QualType t = vd->getType();
  if (!fn || !fn->getBody() || vd->isInvalidDecl() || t.isNull() || t->isDependentType() || t.isConstQualified() ||
      t->isReferenceType() || t->isArrayType() || t->isPointerType() || t->isUndeducedType() || vd->isStaticLocal() ||
      vd->isExceptionVariable() || vd->isCXXForRangeDecl() || fn->isTemplateInstantiation() || fn->isDependentContext() ||
      exempt(*result.SourceManager, vd->getLocation()))
    return;
  ExprMutationAnalyzer analyzer(*fn->getBody(), *result.Context);
  const auto *dre_probe = vd;
  if (analyzer.isMutated(dre_probe))
    return;
  // Range-for calls non-const begin()/end() on its range, which the analyzer does not report as a mutation.
  if (!match(findAll(cxxForRangeStmt(hasRangeInit(ignoringParenImpCasts(declRefExpr(to(equalsNode(vd))))))), *fn->getBody(),
             *result.Context)
           .empty())
    return;
  // `return v;` moves (or elides) only a non-const local; const would turn it into a copy.
  for (const auto &m : match(findAll(returnStmt(hasReturnValue(ignoringParenImpCasts(declRefExpr(to(equalsNode(vd)))))).bind("ret")),
                             *fn->getBody(), *result.Context))
    if (!m.getNodeAs<ReturnStmt>("ret")->getRetValue()->getType()->isReferenceType() && !vd->getType().isTriviallyCopyableType(*result.Context))
      return;
  // The analyzer does not see inline-asm output operands as writes.
  for (const auto &m : match(findAll(asmStmt().bind("asm")), *fn->getBody(), *result.Context)) {
    const auto *as = m.getNodeAs<GCCAsmStmt>("asm");
    for (unsigned i = 0; as && i < as->getNumOutputs(); ++i)
      if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(as->getOutputExpr(i)->IgnoreParenImpCasts()); dre && dre->getDecl() == vd)
        return;
  }
  auto d = diag(vd->getLocation(), "local %0 is never modified; declare it const") << vd;
  // `int a = 1, b = 2;` shares one specifier: only offer the fix when the declaration stands alone.
  bool alone = false;
  for (const DynTypedNode &p : result.Context->getParents(*vd))
    if (const auto *ds = p.get<DeclStmt>())
      alone = ds->isSingleDecl();
  if (alone && vd->getOuterLocStart().isValid() && !vd->getOuterLocStart().isMacroID())
    d << FixItHint::CreateInsertion(vd->getOuterLocStart(), "const ");
}

// ---------------------------------------------------------------------------
// reloco-no-shadow
// ---------------------------------------------------------------------------
void NoShadowCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(varDecl(hasLocalStorage(), unless(isImplicit()), unless(isExpansionInSystemHeader())).bind("var"),
                     this);
}

void NoShadowCheck::check(const MatchFinder::MatchResult &result) {
  const auto *vd = result.Nodes.getNodeAs<VarDecl>("var");
  if (!vd->getIdentifier() || vd->isInvalidDecl() || vd->getName().starts_with("_") ||
      exempt(*result.SourceManager, vd->getLocation()))
    return;
  const SourceManager &sm = *result.SourceManager;
  const auto before = [&](const VarDecl *o) {
    return o != vd && o->getIdentifier() && o->getName() == vd->getName() &&
           sm.isBeforeInTranslationUnit(o->getLocation(), vd->getLocation());
  };
  const VarDecl *hit = nullptr;
  DynTypedNode cur = DynTypedNode::create(*vd);
  while (!hit) {
    auto parents = result.Context->getParents(cur);
    if (parents.empty())
      break;
    cur = parents[0];
    if (const auto *cs = cur.get<CompoundStmt>()) {
      for (const Stmt *s : cs->body())
        if (const auto *ds = llvm::dyn_cast<DeclStmt>(s))
          for (const Decl *d : ds->decls())
            if (const auto *o = llvm::dyn_cast<VarDecl>(d); o && before(o))
              hit = o;
    } else if (const auto *fs = cur.get<ForStmt>()) {
      if (const auto *ds = llvm::dyn_cast_or_null<DeclStmt>(fs->getInit()))
        for (const Decl *d : ds->decls())
          if (const auto *o = llvm::dyn_cast<VarDecl>(d); o && before(o))
            hit = o;
    } else if (const auto *rf = cur.get<CXXForRangeStmt>()) {
      if (rf->getLoopVariable() && before(rf->getLoopVariable()))
        hit = rf->getLoopVariable();
    } else if (const auto *is = cur.get<IfStmt>()) {
      if (is->getConditionVariable() && before(is->getConditionVariable()))
        hit = is->getConditionVariable();
    } else if (const auto *ws = cur.get<WhileStmt>()) {
      if (ws->getConditionVariable() && before(ws->getConditionVariable()))
        hit = ws->getConditionVariable();
    } else if (const auto *ss = cur.get<SwitchStmt>()) {
      if (ss->getConditionVariable() && before(ss->getConditionVariable()))
        hit = ss->getConditionVariable();
    } else if (const auto *fd = cur.get<FunctionDecl>()) {
      for (const ParmVarDecl *p : fd->parameters())
        if (before(p))
          hit = p;
      break;
    }
  }
  if (hit) {
    diag(vd->getLocation(), "%0 shadows a variable declared in an enclosing scope; rename it") << vd;
    diag(hit->getLocation(), "shadowed declaration is here", DiagnosticIDs::Note);
  }
}

// ---------------------------------------------------------------------------
// reloco-enum-switch
// ---------------------------------------------------------------------------
void EnumSwitchCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(switchStmt().bind("sw"), this);
}

void EnumSwitchCheck::check(const MatchFinder::MatchResult &result) {
  const auto *sw = result.Nodes.getNodeAs<SwitchStmt>("sw");
  if (exempt(*result.SourceManager, sw->getSwitchLoc()))
    return;
  const auto *et = sw->getCond()->IgnoreParenImpCasts()->getType()->getAs<EnumType>();
  if (!et) {
    // Integer/char switches have no enumerator list to check, so a 'default' is the only exhaustiveness there is.
    if (sw->getCond()->isTypeDependent() || sw->getCond()->getType()->isBooleanType())
      return;
    bool anyDefault = false;
    for (const SwitchCase *sc = sw->getSwitchCaseList(); sc; sc = sc->getNextSwitchCase())
      anyDefault |= llvm::isa<DefaultStmt>(sc);
    if (!anyDefault) {
      diag(sw->getSwitchLoc(), "switch over %0 has no 'default'; unhandled values are silently ignored")
          << sw->getCond()->getType();
      howToFix(sw->getSwitchLoc(), "add 'default:' that handles or reports the unexpected value (e.g. "
                                   "'default: return error::invalid_argument;')");
    }
    return;
  }
#if RELOCO_TIDY_LLVM_MAJOR >= 22
  const EnumDecl *ed = et->getDecl()->getDefinitionOrSelf();
#else
  const EnumDecl *ed = et->getDecl()->getDefinition();
#endif
  if (!ed)
    return;
  llvm::SmallVector<llvm::APSInt, 8> covered;
  bool hasDefault = false;
  for (const SwitchCase *sc = sw->getSwitchCaseList(); sc; sc = sc->getNextSwitchCase()) {
    if (llvm::isa<DefaultStmt>(sc)) {
      hasDefault = true;
      continue;
    }
    const auto *cs = llvm::cast<CaseStmt>(sc);
    Expr::EvalResult er;
    if (cs->getLHS()->EvaluateAsInt(er, *result.Context))
      covered.push_back(er.Val.getInt());
  }
  for (const EnumConstantDecl *ec : ed->enumerators()) {
    bool found = false;
    for (const llvm::APSInt &v : covered)
      if (llvm::APSInt::isSameValue(v, ec->getInitVal()))
        found = true;
    if (!found) {
      // A 'default' usually only silences -Wswitch / static analyzers and would swallow enumerators added later.
      if (hasDefault)
        diag(sw->getSwitchLoc(), "switch over enum %0 has a 'default' but does not list enumerator %1; 'default' must not "
                                 "stand in for enumerators")
            << ed << ec;
      else
        diag(sw->getSwitchLoc(), "switch over enum %0 does not handle enumerator %1") << ed << ec;
      howToFix(sw->getSwitchLoc(), "add 'case E::name:' for every enumerator (group them with fallthrough labels if they "
                                   "share handling); keep 'default:' only as the extra arm for out-of-range values, so "
                                   "adding an enumerator later still fails to compile; suppress with NOLINT where a "
                                   "catch-all is intended");
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// reloco-banned-low-level
// ---------------------------------------------------------------------------
void BannedLowLevelCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(gotoStmt().bind("goto"), this);
  finder->addMatcher(recordDecl(isUnion(), isDefinition(), unless(isImplicit())).bind("union"), this);
  finder->addMatcher(varDecl(unless(isImplicit()), hasType(isVolatileQualified())).bind("volatile"), this);
  finder->addMatcher(fieldDecl(unless(isImplicit()), hasType(isVolatileQualified())).bind("volatile"), this);
  finder->addMatcher(callExpr(callee(functionDecl(hasAnyName("setjmp", "_setjmp", "sigsetjmp", "longjmp", "_longjmp",
                                                             "siglongjmp"))))
                         .bind("jmp"),
                     this);
}

void BannedLowLevelCheck::check(const MatchFinder::MatchResult &result) {
  const SourceManager &sm = *result.SourceManager;
  if (const auto *g = result.Nodes.getNodeAs<Stmt>("goto")) {
    if (!exempt(sm, g->getBeginLoc()))
    {
      diag(g->getBeginLoc(), "goto is banned; use structured control flow");
      howToFix(g->getBeginLoc(), "use an early 'return', 'break'/'continue', or extract the cleanup into a function or an RAII "
                                 "object instead of jumping to a label");
    }
  } else if (const auto *u = result.Nodes.getNodeAs<RecordDecl>("union")) {
    if (!exempt(sm, u->getLocation()) && !inUnsafeTier(u) && !u->isTemplated())
    {
      diag(u->getLocation(), "unions allow type punning; use a tagged variant");
      howToFix(u->getLocation(), "'reloco::variant<A, B>' for alternatives; hardware register maps belong in an unsafe_* function or a NOLINT "
                                 "region");
    }
  } else if (const auto *d = result.Nodes.getNodeAs<DeclaratorDecl>("volatile")) {
    if (!exempt(sm, d->getLocation()) && !inUnsafeTier(d) && !d->isTemplated())
    {
      diag(d->getLocation(), "volatile is banned outside hardware-access code; suppress with NOLINT where it is MMIO");
      howToFix(d->getLocation(), "use an atomic ('std::atomic<T>') for shared state; for real MMIO keep 'volatile' and add "
                                 "'// NOLINT(reloco-banned-low-level): MMIO register'");
    }
  } else if (const auto *c = result.Nodes.getNodeAs<CallExpr>("jmp")) {
    if (!exempt(sm, c->getBeginLoc()))
    {
      diag(c->getBeginLoc(), "setjmp/longjmp bypass destructors; use explicit error results");
      howToFix(c->getBeginLoc(), "return 'reloco::expected<T, E>' up the call chain instead of jumping");
    }
  }
}

// ---------------------------------------------------------------------------
// reloco-mutable-member
// ---------------------------------------------------------------------------
void MutableMemberCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(fieldDecl(unless(isImplicit()), unless(isExpansionInSystemHeader())).bind("field"),
                     this);
}

void MutableMemberCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fd = result.Nodes.getNodeAs<FieldDecl>("field");
  const QualType t = fd->getType();
  if (!fd->isMutable() || fd->isInvalidDecl() || t.isNull() ||
      exempt(*result.SourceManager, fd->getLocation()) || inUnsafeTier(fd) || fd->hasAttr<GuardedByAttr>() || fd->hasAttr<PtGuardedByAttr>())
    return;
  // Atomics, locks and reloco's own cells are the sanctioned interior-mutability types (Rust's Cell/RefCell/Mutex).
  if (const CXXRecordDecl *rd = recordOf(result.Context->getBaseElementType(t))) {
    const std::string n = rd->getName().lower();
    for (const char *w : {"atomic", "mutex", "once_flag", "condition", "spinlock", "lock", "cell"})
      if (n.find(w) != std::string::npos)
        return;
  }
  if (t->isDependentType()) {
    const std::string n = llvm::StringRef(t.getAsString()).lower();
    for (const char *w : {"atomic", "mutex", "once_flag", "condition", "lock", "cell"})
      if (n.find(w) != std::string::npos)
        return;
  }
  diag(fd->getLocation(), "mutable member %0 hides mutation behind a const interface") << fd;
  howToFix(fd->getLocation(), "wrap it in 'reloco::cell<T>' / an atomic / a lock, mark it RELOCO_GUARDED_BY(mutex_), or "
                              "drop 'mutable' and make the method non-const");
}

// ---------------------------------------------------------------------------
// reloco-mutable-global
// ---------------------------------------------------------------------------
void MutableGlobalCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(varDecl(hasGlobalStorage(), unless(parmVarDecl()), unless(isImplicit()), unless(isConstexpr()),
                             unless(isExpansionInSystemHeader()))
                         .bind("var"),
                     this);
}

void MutableGlobalCheck::check(const MatchFinder::MatchResult &result) {
  const auto *vd = result.Nodes.getNodeAs<VarDecl>("var");
  const QualType t = vd->getType();
  if (vd->isInvalidDecl() || t.isNull() || t->isDependentType() || t.isConstQualified() || vd->isTemplated() ||
      vd->isExternC() || vd->isStaticDataMember() == false && vd->isFileVarDecl() == false && vd->isStaticLocal() == false ||
      exempt(*result.SourceManager, vd->getLocation()) || inUnsafeTier(vd))
    return;
  // Synchronisation primitives are the sanctioned way to share state (Rust's static atomics/Mutex).
  if (const CXXRecordDecl *rd = recordOf(result.Context->getBaseElementType(t))) {
    const std::string n = rd->getName().lower();
    for (const char *w : {"atomic", "mutex", "once_flag", "condition", "spinlock", "lock"})
      if (n.find(w) != std::string::npos)
        return;
  }
  diag(vd->getLocation(), "mutable global/static %0; make it const, or wrap it in an atomic or lock type") << vd;
  howToFix(vd->getLocation(), "'static int count;' becomes 'static std::atomic<int> count{};', or pass the state in as a "
                              "parameter/member ('struct ctx { int count = 0; };') instead of sharing it globally");
}

// ---------------------------------------------------------------------------
// reloco-unsafe-call-scope
// ---------------------------------------------------------------------------
void UnsafeCallScopeCheck::registerMatchers(MatchFinder *finder) {
  const auto enclosing = hasAncestor(functionDecl().bind("encl"));
  finder->addMatcher(callExpr(callee(functionDecl().bind("callee")), optionally(enclosing)).bind("call"), this);
  finder->addMatcher(cxxConstructExpr(hasDeclaration(cxxConstructorDecl().bind("callee")), optionally(enclosing))
                         .bind("call"),
                     this);
}

void UnsafeCallScopeCheck::check(const MatchFinder::MatchResult &result) {
  const auto *callee = result.Nodes.getNodeAs<FunctionDecl>("callee");
  const auto *call = result.Nodes.getNodeAs<Expr>("call");
  if (!callee || !call || !inUnsafeTier(callee))
    return;
  if (const FunctionDecl *encl = enclosingFunction(result); encl && inUnsafeTier(encl))
    return;
  if (exempt(*result.SourceManager, call->getBeginLoc()))
    return;
  diag(call->getBeginLoc(), "call to unsafe %0 outside the unsafe tier; call it from an unsafe_* function or wrap it in "
                            "RELOCO_BEGIN_UNSAFE_CODE / RELOCO_END_UNSAFE_CODE")
      << callee;
  howToFix(call->getBeginLoc(), "prefer the safe overload (span/result based); otherwise move this code into 'unsafe_xyz()' or "
                                "wrap just the call: 'RELOCO_BEGIN_UNSAFE_CODE raw_poke(p); RELOCO_END_UNSAFE_CODE' with a "
                                "comment stating the invariant that makes it sound");
}

// ---------------------------------------------------------------------------
// reloco-checked-operator
// ---------------------------------------------------------------------------
namespace {
bool isGuardedOperator(const FunctionDecl *fd) {
  const OverloadedOperatorKind k = fd->getOverloadedOperator();
  if (k == OO_Subscript || k == OO_Arrow)
    return true;
  if (k != OO_Star)
    return false;
  const unsigned arity = fd->getNumParams() + (llvm::isa<CXXMethodDecl>(fd) && !fd->isStatic() ? 1u : 0u);
  return arity == 1; // unary dereference, not multiplication
}

// Dereferencing an iterator is guarded by the iterator protocol (compare with end() first); an iterator cannot bounds
// check itself, so `*it` / `it->` on iterator types is not a checked-access defect.
bool isIteratorRecord(const CXXRecordDecl *rd) {
  if (!rd || !rd->hasDefinition())
    return false;
  if (rd->getName().ends_with("iterator") || rd->getName().ends_with("_iterator"))
    return true;
  const auto has = [](const CXXRecordDecl *c) {
    for (const Decl *d : c->decls())
      if (const auto *nd = llvm::dyn_cast<TypedefNameDecl>(d); nd && nd->getName() == "iterator_category")
        return true;
    return false;
  };
  if (has(rd))
    return true;
  bool found = false;
  rd->forallBases([&](const CXXRecordDecl *b) {
    if (has(b))
      found = true;
    return !found;
  });
  return found;
}
} // namespace

void CheckedOperatorCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(unless(isImplicit()), unless(isDeleted()),
                                  hasAnyOverloadedOperatorName("[]", "*", "->"))
                         .bind("def"),
                     this);
  finder->addMatcher(cxxOperatorCallExpr(hasAnyOverloadedOperatorName("[]", "*", "->"),
                                         optionally(hasAncestor(functionDecl().bind("encl"))))
                         .bind("use"),
                     this);
}

void CheckedOperatorCheck::check(const MatchFinder::MatchResult &result) {
  const SourceManager &sm = *result.SourceManager;
  if (const auto *fd = result.Nodes.getNodeAs<FunctionDecl>("def")) {
    if (!isGuardedOperator(fd) || fd->isTemplateInstantiation() || fd->getCanonicalDecl() != fd || exempt(sm, fd->getLocation()) ||
        inUnsafeTier(fd) || annotatedInScope(fd, "reloco_checked"))
      return;
    diag(fd->getLocation(), "%0 must be RELOCO_CHECKED (it must bounds/null check), or be an unsafe_* function") << fd;
    howToFix(fd->getLocation(), "make it check and then annotate: 'RELOCO_CHECKED T &operator[](size_t i) & noexcept' (clamp, "
                                "assert or trap on out-of-range), or rename it 'unsafe_at' and offer a checked 'at()' instead");
    return;
  }
  const auto *oc = result.Nodes.getNodeAs<CXXOperatorCallExpr>("use");
  const FunctionDecl *callee = oc ? oc->getDirectCallee() : nullptr;
  if (!callee || !isGuardedOperator(callee) || exempt(sm, oc->getBeginLoc()))
    return;
  if (annotatedInScope(callee, "reloco_checked"))
    return;
  if (callee->getOverloadedOperator() != OO_Subscript)
    if (const auto *m = llvm::dyn_cast<CXXMethodDecl>(callee); m && isIteratorRecord(m->getParent()))
      return;
  if (const FunctionDecl *encl = enclosingFunction(result); encl && inUnsafeTier(encl))
    return;
  diag(oc->getBeginLoc(), "use of unchecked %0; the type does not declare it RELOCO_CHECKED, use a checked accessor") << callee;
  howToFix(oc->getBeginLoc(), "call the checked accessor ('v.try_at(i)' returning a result, or 'front()'/'back()' which assert) or iterate "
                              "with a range-for; inside an unsafe_* function or unsafe region the operator is allowed");
}

// ---------------------------------------------------------------------------
// reloco-safe-math
// ---------------------------------------------------------------------------
void SafeMathCheck::registerMatchers(MatchFinder *finder) {
  const auto enclosing = hasAncestor(functionDecl().bind("encl"));
  finder->addMatcher(binaryOperator(hasAnyOperatorName("+", "-", "*", "/", "%", "<<", ">>", "+=", "-=", "*=", "/=", "%=",
                                                       "<<=", ">>="),
                                    enclosing)
                         .bind("op"),
                     this);
  finder->addMatcher(unaryOperator(hasAnyOperatorName("++", "--", "-"), enclosing).bind("op"), this);
}

void SafeMathCheck::check(const MatchFinder::MatchResult &result) {
  const auto *e = result.Nodes.getNodeAs<Expr>("op");
  const FunctionDecl *fn = enclosingFunction(result);
  if (!e || !fn || e->isValueDependent() || e->getType().isNull() || !e->getType()->isIntegerType() ||
      e->getType()->isBooleanType() || e->getType()->isEnumeralType())
    return;
  if (!annotatedInScope(fn, "reloco_safe_math") || inUnsafeTier(fn) || exempt(*result.SourceManager, e->getBeginLoc()))
    return;
  Expr::EvalResult er;
  if (e->EvaluateAsRValue(er, *result.Context))
    return; // compile-time constants are checked by the compiler
  diag(e->getBeginLoc(), "raw integer arithmetic in RELOCO_SAFE_MATH code; use checked/saturating/wrapping helpers "
                         "(reloco/checked.hpp, saturating.hpp, wrapping.hpp)");
  howToFix(e->getBeginLoc(), "'a + b' becomes 'checked_add(a, b)' (returns an optional/result), 'saturating_add(a, b)' to clamp, "
                             "or 'wrapping_add(a, b)' when modular arithmetic is intended");
}

// ---------------------------------------------------------------------------
// reloco-member-init
// ---------------------------------------------------------------------------
void MemberInitCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(fieldDecl(unless(isImplicit()), unless(hasInClassInitializer(anything())),
                               unless(isExpansionInSystemHeader()))
                         .bind("field"),
                     this);
}

void MemberInitCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fd = result.Nodes.getNodeAs<FieldDecl>("field");
  const RecordDecl *parent = fd->getParent();
  const QualType t = fd->getType();
  if (const auto *crd = dyn_cast<CXXRecordDecl>(parent);
      crd && (crd->getTemplateSpecializationKind() != TSK_Undeclared || crd->getInstantiatedFromMemberClass()))
    return;
  if (fd->isInvalidDecl() || t.isNull() || t->isDependentType() || parent->isUnion() || parent->isLambda() ||
      t->isReferenceType() || fd->isTemplated() || fd->isUnnamedBitField() ||
      (fd->isBitField() && !result.Context->getLangOpts().CPlusPlus20) || // no default member initializer before C++20
      exempt(*result.SourceManager, fd->getLocation()) || inUnsafeTier(fd))
    return;
  const QualType base = result.Context->getBaseElementType(t);
  if (const CXXRecordDecl *rd = base->getAsCXXRecordDecl()) {
    if (!rd->hasDefinition() || !rd->hasTrivialDefaultConstructor())
      return; // initialises itself
  }
  auto d = diag(fd->getLocation(), "field %0 has no default member initializer; add '= {}' or a value") << fd;
  // A default member initializer in a nested class cannot be used by the enclosing class before it is complete
  // (e.g. in its static constexpr members), so only offer the fix-it for non-nested records.
  if (fd->getEndLoc().isValid() && !fd->getEndLoc().isMacroID() && !fd->isBitField() && !t->isIncompleteArrayType() &&
      !parent->getDeclContext()->isRecord())
    d << FixItHint::CreateInsertion(Lexer::getLocForEndOfToken(fd->getEndLoc(), 0, *result.SourceManager,
                                                              result.Context->getLangOpts()),
                                    "{}");
}

// ---------------------------------------------------------------------------
// reloco-rule-of-five
// ---------------------------------------------------------------------------
void RuleOfFiveCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit()), unless(isLambda()),
                                   unless(isExpansionInSystemHeader()))
                         .bind("rec"),
                     this);
}

void RuleOfFiveCheck::check(const MatchFinder::MatchResult &result) {
  const auto *rd = result.Nodes.getNodeAs<CXXRecordDecl>("rec");
  if (rd->isUnion() || rd->isTemplated() == false && rd->isDependentContext() || exempt(*result.SourceManager, rd->getLocation()))
    return;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd);
      spec && spec->getSpecializationKind() != TSK_ExplicitSpecialization)
    return;
  const bool dtor = rd->hasUserDeclaredDestructor();
  const bool cctor = rd->hasUserDeclaredCopyConstructor();
  const bool cassign = rd->hasUserDeclaredCopyAssignment();
  const bool mctor = rd->hasUserDeclaredMoveConstructor();
  const bool massign = rd->hasUserDeclaredMoveAssignment();
  const int n = dtor + cctor + cassign + mctor + massign;
  if (n == 0 || n == 5)
    return;
  // A lone defaulted (often virtual) destructor is the idiomatic way to declare a base class.
  if (n == 1 && dtor) {
    if (const CXXDestructorDecl *dd = rd->getDestructor(); dd && dd->isExplicitlyDefaulted())
      return;
  }
  // Deleting both copy operations makes the type non-copyable and (implicitly) non-movable on purpose; the
  // remaining members then need no declaration.
  const auto isDeletedCopy = [&](bool copyCtor) {
    for (const CXXMethodDecl *m : rd->methods()) {
      if (copyCtor ? (llvm::isa<CXXConstructorDecl>(m) && llvm::cast<CXXConstructorDecl>(m)->isCopyConstructor())
                   : m->isCopyAssignmentOperator())
        return m->isDeleted();
    }
    return false;
  };
  if (cctor && cassign && isDeletedCopy(true) && isDeletedCopy(false))
    return;
  std::string missing;
  const auto add = [&](bool have, const char *name) {
    if (!have)
      missing += (missing.empty() ? "" : ", ") + std::string(name);
  };
  add(dtor, "destructor");
  add(cctor, "copy constructor");
  add(cassign, "copy assignment");
  add(mctor, "move constructor");
  add(massign, "move assignment");
  diag(rd->getLocation(), "%0 declares some special members but not: %1; declare all five (= default / = delete)")
      << rd << missing;
  howToFix(rd->getLocation(), "'T(const T &) = delete; T &operator=(const T &) = delete; T(T &&) noexcept = default; "
                              "T &operator=(T &&) noexcept = default; ~T() = default;' for a move-only type");
}

} // namespace reloco::tidy
