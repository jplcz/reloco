// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"
#include "LifetimeNames.h"

#include "clang/Lex/Lexer.h"

#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace reloco::tidy {

namespace {

bool refersToOwnClass(QualType t, const CXXRecordDecl *rd) {
  const QualType pointee = t->isPointerType() ? t->getPointeeType() : t.getNonReferenceType();
  return pointee.getCanonicalType()->getAsCXXRecordDecl() == rd;
}

// `T &f() const & noexcept RELOCO_LIFETIMEBOUND` is a type attribute on the method's function type.
bool thisIsLifetimeBound(const CXXMethodDecl *md, const ASTContext &ctx) {
  if (md->hasAttr<LifetimeBoundAttr>() || sharesLifetime(declThisLifetimes(md), declReturnLifetimes(md)))
    return true;
  // Walk every sugar layer: other type attributes (RELOCO_CALLABLE_WHEN) can wrap the lifetimebound one.
  for (QualType t = md->getType();;) {
    if (const auto *at = llvm::dyn_cast<AttributedType>(t.getTypePtr()); at && at->getAttrKind() == attr::LifetimeBound)
      return true;
    const QualType next = t.getSingleStepDesugaredType(ctx);
    if (next == t)
      return false;
    t = next;
  }
}

bool hasCopiesAnnotation(const ParmVarDecl *p) {
  for (const auto *an : p->specific_attrs<AnnotateAttr>())
    if (an->getAnnotation() == "reloco_copies")
      return true;
  return false;
}

bool hasBorrowAnnotation(const ParmVarDecl *p) {
  if (!paramLifetimes(p).empty())
    return true;
#if RELOCO_TIDY_LLVM_MAJOR >= 20
  return p->hasAttr<LifetimeBoundAttr>() || p->hasAttr<LifetimeCaptureByAttr>();
#else
  return p->hasAttr<LifetimeBoundAttr>();
#endif
}

bool aliasesStorage(QualType t) {
  if (t.isNull() || t->isDependentType())
    return false;
  if (t->isReferenceType() || t->isPointerType())
    return true;
  const CXXRecordDecl *rd = t.getCanonicalType()->getAsCXXRecordDecl();
  return rd && rd->hasDefinition() && rd->hasAttr<PointerAttr>();
}

bool mentions(const Stmt *s, const ParmVarDecl *p) {
  struct V : RecursiveASTVisitor<V> {
    const ParmVarDecl *p;
    bool found = false;
    bool VisitDeclRefExpr(DeclRefExpr *e) {
      found |= e->getDecl() == p;
      return !found;
    }
  } v;
  v.p = p;
  v.TraverseStmt(const_cast<Stmt *>(s));
  return v.found;
}

// Whether the value of `e` can carry a borrow of `p`: scalars read out of the pointee and copy/move constructions
// are copies; a glvalue bound to a reference, an address or a view built from it keeps the pointee alive.
bool flows(const Expr *e, const ParmVarDecl *p) {
  if (!e)
    return false;
  e = e->IgnoreParens();
  if (const auto *ic = llvm::dyn_cast<ImplicitCastExpr>(e)) {
    if (ic->getCastKind() == CK_LValueToRValue)
      return false;
    return flows(ic->getSubExpr(), p);
  }
  if (const auto *mt = llvm::dyn_cast<MaterializeTemporaryExpr>(e))
    return flows(mt->getSubExpr(), p);
  if (const auto *bt = llvm::dyn_cast<CXXBindTemporaryExpr>(e))
    return flows(bt->getSubExpr(), p);
  if (const auto *ce = llvm::dyn_cast<CXXConstructExpr>(e)) {
    if (ce->getConstructor()->isCopyOrMoveConstructor())
      return false;
    for (const Expr *a : ce->arguments())
      if (flows(a, p))
        return true;
    return false;
  }
  if (const auto *call = llvm::dyn_cast<CallExpr>(e)) {
    for (const Expr *a : call->arguments())
      if (flows(a, p))
        return true;
    return false;
  }
  if (const auto *pl = llvm::dyn_cast<ParenListExpr>(e)) {
    for (unsigned i = 0; i < pl->getNumExprs(); ++i) // exprs() is non-const before LLVM 23
      if (flows(pl->getExpr(i), p))
        return true;
    return false;
  }
  return (e->isGLValue() || aliasesStorage(e->getType())) && mentions(e, p);
}

// Whether the constructor stores something derived from `p` into a member that aliases (pointer, reference, view):
// `dev_(dev)` with a by-value member copies the pointee and borrows nothing. Declarations without a body, delegating
// and base initialisers are answered conservatively with `true`.
bool storesIntoAliasingMember(const CXXConstructorDecl *ctor, const ParmVarDecl *p) {
  if (!ctor->hasBody())
    return true;
  for (const CXXCtorInitializer *init : ctor->inits()) {
    if (init->isDelegatingInitializer()) {
      // The target constructor is checked on its own: the argument borrows only when it feeds an annotated parameter.
      if (const auto *ce = llvm::dyn_cast<CXXConstructExpr>(init->getInit()->IgnoreImplicit())) {
        const CXXConstructorDecl *target = ce->getConstructor();
        for (unsigned i = 0; i < ce->getNumArgs() && i < target->getNumParams(); ++i)
          if (hasBorrowAnnotation(target->getParamDecl(i)) && flows(ce->getArg(i), p))
            return true;
        continue;
      }
    }
    if (!init->isMemberInitializer()) {
      if (mentions(init->getInit(), p))
        return true;
      continue;
    }
    if (aliasesStorage(init->getMember()->getType()) && flows(init->getInit(), p))
      return true;
  }
  struct V : RecursiveASTVisitor<V> {
    const ParmVarDecl *p;
    bool found = false;
    bool VisitBinaryOperator(BinaryOperator *b) {
      if (b->isAssignmentOp() && aliasesStorage(b->getLHS()->getType()) && flows(b->getRHS(), p))
        found = true;
      return !found;
    }
    bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *c) {
      if (c->isAssignmentOp() && c->getNumArgs() == 2 && aliasesStorage(c->getArg(0)->getType()) &&
          flows(c->getArg(1), p))
        found = true;
      return !found;
    }
  } v;
  v.p = p;
  v.TraverseStmt(ctor->getBody());
  return v.found;
}

} // namespace

void LifetimeAnnotationsCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxMethodDecl(unless(isImplicit()), unless(isStaticStorageClass()), unless(cxxConstructorDecl()),
                                   unless(cxxDestructorDecl()), unless(isDeleted()))
                         .bind("method"),
                     this);
  finder->addMatcher(
      cxxConstructorDecl(unless(isImplicit()), unless(isCopyConstructor()), unless(isMoveConstructor())).bind("ctor"),
      this);
}

void LifetimeAnnotationsCheck::check(const MatchFinder::MatchResult &result) {
  if (const auto *md = result.Nodes.getNodeAs<CXXMethodDecl>("method")) {
    const CXXRecordDecl *rd = md->getParent();
    if (md->getCanonicalDecl() != md || md->isTemplateInstantiation() || rd->isLambda() ||
        rd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation || md->isVirtual() ||
        exempt(*result.SourceManager, md->getLocation()))
      return;
    // Handles (gsl::pointer) hand out what they point to, not their own storage.
    if (recordIsPointer(rd) || md->isCopyAssignmentOperator() || md->isMoveAssignmentOperator() || inUnsafeTier(md))
      return;
    const QualType ret = md->getReturnType();
    if (!isAliasing(ret) || isStaticStrType(ret, *result.Context) || refersToOwnClass(ret, rd) ||
        thisIsLifetimeBound(md, *result.Context))
      return;
    // Scoped: LLVM 19 allows only one live DiagnosticBuilder, so the note is emitted after it is destroyed.
    {
      auto d =
          diag(md->getLocation(), "member %0 returns an alias of its object; annotate it with RELOCO_LIFETIMEBOUND")
          << md;
      // Insert after the cv/ref/noexcept qualifiers; a trailing return type or requires-clause is left to the user.
      const SourceManager &sm = *result.SourceManager;
      const LangOptions &lo = result.Context->getLangOpts();
      if (const SourceLocation at = functionQualifierEnd(md, sm, lo, true, true); at.isValid() && !at.isMacroID()) {
        const auto next = Lexer::findNextToken(at.getLocWithOffset(-1), sm, lo);
        const bool plain = next && !next->is(tok::arrow) &&
                           !(next->is(tok::raw_identifier) &&
                             (next->getRawIdentifier() == "requires" || next->getRawIdentifier() == "try"));
        if (plain)
          d << FixItHint::CreateInsertion(at, " RELOCO_LIFETIMEBOUND");
      }
    }
    howToFix(md->getLocation(),
             "append the macro after the qualifiers: 'int &first() & noexcept RELOCO_LIFETIMEBOUND;' so Clang "
             "rejects 'auto &r = make().first();'");
    return;
  }

  if (const auto *ctor = result.Nodes.getNodeAs<CXXConstructorDecl>("ctor")) {
    const CXXRecordDecl *rd = ctor->getParent();
    if (ctor->getCanonicalDecl() != ctor || !recordIsPointer(rd) ||
        rd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation ||
        exempt(*result.SourceManager, ctor->getLocation()))
      return;
    for (const ParmVarDecl *p : ctor->parameters()) {
      const QualType t = p->getType();
      if (t.isNull() || t->isDependentType() || !(t->isPointerType() || t->isLValueReferenceType()))
        continue;
      if (t->isFunctionPointerType() || isPointerLike(t) || hasBorrowAnnotation(p) || hasCopiesAnnotation(p) || !storesIntoAliasingMember(ctor, p))
        continue;
      diag(p->getLocation(), "handle constructor borrows parameter %0; annotate it with RELOCO_LIFETIMEBOUND or "
                             "RELOCO_LIFETIME_CAPTURE_BY_THIS")
          << p;
      howToFix(p->getLocation(), "'view(const buf &b RELOCO_LIFETIMEBOUND)' for a handle that borrows the argument, or "
                                 "'RELOCO_LIFETIME_CAPTURE_BY_THIS' when the parameter is stored into *this");
    }
  }
}

} // namespace reloco::tidy
