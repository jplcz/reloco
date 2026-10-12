// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/Analysis/Analyses/ExprMutationAnalyzer.h"
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
  diag(ce->getBeginLoc(), "implicit conversion from %0 to %1 may change the value; convert explicitly (static_cast or a "
                          "checked helper)")
      << src << dst;
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

void ExplicitConversionCheck::check(const MatchFinder::MatchResult &result) {
  if (const auto *c = result.Nodes.getNodeAs<CXXConstructorDecl>("ctor")) {
    if (c->getNumParams() == 0 || c->getMinRequiredArguments() > 1 || c->getParent()->isLambda() ||
        c->isTemplateInstantiation() || exempt(*result.SourceManager, c->getLocation()))
      return;
    const CXXRecordDecl *pr = recordOf(c->getParamDecl(0)->getType());
    if (pr && pr->getIdentifier() && pr->getName() == "initializer_list")
      return;
    diag(c->getLocation(), "single-argument constructor of '%0' must be explicit") << c->getParent()->getName();
  } else if (const auto *cv = result.Nodes.getNodeAs<CXXConversionDecl>("conv")) {
    if (cv->isTemplateInstantiation() || cv->getParent()->isLambda() || exempt(*result.SourceManager, cv->getLocation()))
      return;
    diag(cv->getLocation(), "conversion operator must be explicit");
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
  if (fn->getCanonicalDecl() != fn || fn->isTemplateInstantiation() || fn->getReturnType()->isReferenceType() ||
      exempt(*result.SourceManager, fn->getLocation()))
    return;
  const CXXRecordDecl *rd = recordOf(fn->getReturnType());
  if (!isFallibleName(rd) || fn->hasAttr<WarnUnusedResultAttr>() || rd->hasAttr<WarnUnusedResultAttr>())
    return;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
    if (spec->getSpecializedTemplate()->getTemplatedDecl()->hasAttr<WarnUnusedResultAttr>())
      return;
  diag(fn->getLocation(), "'%0' returns a fallible '%1' and must be [[nodiscard]] (or the type must be)")
      << fn << rd->getName();
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
  if (!call || call->getType().isNull() || call->getType()->isReferenceType() ||
      exempt(*result.SourceManager, call->getBeginLoc()))
    return;
  if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(call); oc && oc->isAssignmentOp())
    return;
  if (!isFallibleName(recordOf(call->getType())))
    return;
  diag(call->getBeginLoc(), "result of fallible '%0' is discarded; handle it or cast to void with a reason")
      << recordOf(call->getType())->getName();
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
  diag(vd->getLocation(), "local '%0' is never modified; declare it const") << vd;
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
    diag(vd->getLocation(), "'%0' shadows a variable declared in an enclosing scope; rename it") << vd;
    diag(hit->getLocation(), "shadowed declaration is here", DiagnosticIDs::Note);
  }
}

// ---------------------------------------------------------------------------
// reloco-enum-switch
// ---------------------------------------------------------------------------
void EnumSwitchCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(switchStmt(hasCondition(expr(hasType(hasUnqualifiedDesugaredType(enumType()))))).bind("sw"), this);
}

void EnumSwitchCheck::check(const MatchFinder::MatchResult &result) {
  const auto *sw = result.Nodes.getNodeAs<SwitchStmt>("sw");
  if (exempt(*result.SourceManager, sw->getSwitchLoc()))
    return;
  const auto *et = sw->getCond()->IgnoreParenImpCasts()->getType()->getAs<EnumType>();
  if (!et)
    return;
  const EnumDecl *ed = et->getDecl()->getDefinitionOrSelf();
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
  if (hasDefault) {
    diag(sw->getSwitchLoc(), "switch over enum '%0' must list every enumerator instead of using 'default', so a new "
                             "enumerator breaks the build")
        << ed;
    return;
  }
  for (const EnumConstantDecl *ec : ed->enumerators()) {
    bool found = false;
    for (const llvm::APSInt &v : covered)
      if (llvm::APSInt::isSameValue(v, ec->getInitVal()))
        found = true;
    if (!found) {
      diag(sw->getSwitchLoc(), "switch over enum '%0' does not handle enumerator '%1'") << ed << ec;
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
      diag(g->getBeginLoc(), "goto is banned; use structured control flow");
  } else if (const auto *u = result.Nodes.getNodeAs<RecordDecl>("union")) {
    if (!exempt(sm, u->getLocation()) && !inUnsafeTier(u) && !u->isTemplated())
      diag(u->getLocation(), "unions allow type punning; use a tagged variant");
  } else if (const auto *d = result.Nodes.getNodeAs<DeclaratorDecl>("volatile")) {
    if (!exempt(sm, d->getLocation()) && !inUnsafeTier(d) && !d->isTemplated())
      diag(d->getLocation(), "volatile is banned outside hardware-access code; suppress with NOLINT where it is MMIO");
  } else if (const auto *c = result.Nodes.getNodeAs<CallExpr>("jmp")) {
    if (!exempt(sm, c->getBeginLoc()))
      diag(c->getBeginLoc(), "setjmp/longjmp bypass destructors; use explicit error results");
  }
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
  diag(vd->getLocation(), "mutable global/static '%0'; make it const, or wrap it in an atomic or lock type") << vd;
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
  diag(call->getBeginLoc(), "call to unsafe '%0' outside the unsafe tier; call it from an unsafe_* function or wrap it in "
                            "RELOCO_BEGIN_UNSAFE_CODE / RELOCO_END_UNSAFE_CODE")
      << callee;
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
    diag(fd->getLocation(), "'%0' must be RELOCO_CHECKED (it must bounds/null check), or be an unsafe_* function") << fd;
    return;
  }
  const auto *oc = result.Nodes.getNodeAs<CXXOperatorCallExpr>("use");
  const FunctionDecl *callee = oc ? oc->getDirectCallee() : nullptr;
  if (!callee || !isGuardedOperator(callee) || exempt(sm, oc->getBeginLoc()))
    return;
  if (annotatedInScope(callee, "reloco_checked"))
    return;
  if (const FunctionDecl *encl = enclosingFunction(result); encl && inUnsafeTier(encl))
    return;
  diag(oc->getBeginLoc(), "use of unchecked '%0'; the type does not declare it RELOCO_CHECKED, use a checked accessor") << callee;
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
  if (fd->isInvalidDecl() || t.isNull() || t->isDependentType() || parent->isUnion() || parent->isLambda() ||
      t->isReferenceType() || fd->isTemplated() || fd->isUnnamedBitField() ||
      exempt(*result.SourceManager, fd->getLocation()) || inUnsafeTier(fd))
    return;
  const QualType base = result.Context->getBaseElementType(t);
  if (const CXXRecordDecl *rd = base->getAsCXXRecordDecl()) {
    if (!rd->hasDefinition() || !rd->hasTrivialDefaultConstructor())
      return; // initialises itself
  }
  diag(fd->getLocation(), "field '%0' has no default member initializer; add '= {}' or a value") << fd;
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
  diag(rd->getLocation(), "'%0' declares some special members but not: %1; declare all five (= default / = delete)")
      << rd << missing;
}

} // namespace reloco::tidy
