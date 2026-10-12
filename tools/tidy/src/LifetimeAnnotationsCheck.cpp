// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/Attr.h"

namespace reloco::tidy {

namespace {

bool refersToOwnClass(QualType t, const CXXRecordDecl *rd) {
  const QualType pointee = t->isPointerType() ? t->getPointeeType() : t.getNonReferenceType();
  return pointee.getCanonicalType()->getAsCXXRecordDecl() == rd;
}

// `T &f() const & noexcept RELOCO_LIFETIMEBOUND` is a type attribute on the method's function type.
bool thisIsLifetimeBound(const CXXMethodDecl *md, const ASTContext &ctx) {
  if (md->hasAttr<LifetimeBoundAttr>())
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

bool hasBorrowAnnotation(const ParmVarDecl *p) {
  return p->hasAttr<LifetimeBoundAttr>() || p->hasAttr<LifetimeCaptureByAttr>();
}

} // namespace

void LifetimeAnnotationsCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxMethodDecl(unless(isImplicit()), unless(isStaticStorageClass()), unless(cxxConstructorDecl()),
                                   unless(cxxDestructorDecl()), unless(isDeleted()))
                         .bind("method"),
                     this);
  finder->addMatcher(cxxConstructorDecl(unless(isImplicit()), unless(isCopyConstructor()), unless(isMoveConstructor()))
                         .bind("ctor"),
                     this);
}

void LifetimeAnnotationsCheck::check(const MatchFinder::MatchResult &result) {
  if (const auto *md = result.Nodes.getNodeAs<CXXMethodDecl>("method")) {
    const CXXRecordDecl *rd = md->getParent();
    if (md->getCanonicalDecl() != md || md->isTemplateInstantiation() || rd->isLambda() || rd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation ||
        md->isVirtual() || exempt(*result.SourceManager, md->getLocation()))
      return;
    // Handles (gsl::pointer) hand out what they point to, not their own storage.
    if (recordIsPointer(rd) || md->isCopyAssignmentOperator() || md->isMoveAssignmentOperator() ||
        inUnsafeTier(md))
      return;
    const QualType ret = md->getReturnType();
    if (!isAliasing(ret) || isStaticStrType(ret, *result.Context) || refersToOwnClass(ret, rd) || thisIsLifetimeBound(md, *result.Context))
      return;
    diag(md->getLocation(), "member %0 returns an alias of its object; annotate it with RELOCO_LIFETIMEBOUND") << md;
    howToFix(md->getLocation(), "append the macro after the qualifiers: 'int &first() & noexcept RELOCO_LIFETIMEBOUND;' so Clang "
                                "rejects 'auto &r = make().first();'");
    return;
  }

  if (const auto *ctor = result.Nodes.getNodeAs<CXXConstructorDecl>("ctor")) {
    const CXXRecordDecl *rd = ctor->getParent();
    if (ctor->getCanonicalDecl() != ctor || !recordIsPointer(rd) || rd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation ||
        exempt(*result.SourceManager, ctor->getLocation()))
      return;
    for (const ParmVarDecl *p : ctor->parameters()) {
      const QualType t = p->getType();
      if (t.isNull() || t->isDependentType() || !(t->isPointerType() || t->isLValueReferenceType()))
        continue;
      if (t->isFunctionPointerType() || isPointerLike(t) || hasBorrowAnnotation(p))
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
