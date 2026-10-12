// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

namespace reloco::tidy {

void VirtualCallInCtorCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxMemberCallExpr(on(cxxThisExpr()), callee(cxxMethodDecl(isVirtual())),
                                       forCallable(cxxMethodDecl(anyOf(cxxConstructorDecl(), cxxDestructorDecl())).bind("fn")))
                         .bind("call"),
                     this);
}

void VirtualCallInCtorCheck::check(const MatchFinder::MatchResult &result) {
  const auto *call = result.Nodes.getNodeAs<CXXMemberCallExpr>("call");
  const auto *fn = result.Nodes.getNodeAs<CXXMethodDecl>("fn");
  const CXXMethodDecl *callee = call->getMethodDecl();
  if (!call || !fn || !callee || exempt(*result.SourceManager, call->getBeginLoc()) || inUnsafeTier(fn))
    return;
  // `Base::f()` binds statically, and a final class or method cannot be overridden further.
  if (const auto *me = llvm::dyn_cast<MemberExpr>(call->getCallee()); me && me->hasQualifier())
    return;
  if (callee->hasAttr<FinalAttr>() || fn->getParent()->hasAttr<FinalAttr>())
    return;
  diag(call->getBeginLoc(), "virtual call to %0 from a %select{constructor|destructor}1 does not reach derived "
                            "overrides: the dynamic type is the class being constructed or destroyed")
      << callee << llvm::isa<CXXDestructorDecl>(fn);
  howToFix(call->getBeginLoc(), "call a non-virtual helper, qualify the call ('Base::f()') to make the static "
                                "binding explicit, mark the class or method 'final', or move the work to an "
                                "'init()' step run after construction");
}

} // namespace reloco::tidy

namespace reloco::tidy {

// reloco-slicing: copy/move-constructing a polymorphic base from a derived object drops the derived part.
void SlicingCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxConstructExpr(hasDeclaration(cxxConstructorDecl(anyOf(isCopyConstructor(), isMoveConstructor()))),
                                      argumentCountIs(1), unless(forCallable(isImplicit())))
                         .bind("construct"),
                     this);
}

void SlicingCheck::check(const MatchFinder::MatchResult &result) {
  const auto *ce = result.Nodes.getNodeAs<CXXConstructExpr>("construct");
  if (!ce || exempt(*result.SourceManager, ce->getBeginLoc()))
    return;
  const CXXRecordDecl *base = ce->getConstructor()->getParent();
  const Expr *arg = ce->getArg(0)->IgnoreImpCasts();
  const CXXRecordDecl *derived = arg->getType().getNonReferenceType()->getAsCXXRecordDecl();
  if (!base || !derived || !base->hasDefinition() || !derived->hasDefinition() || base->isDependentType() ||
      derived->isDependentType() || !base->isPolymorphic() || base->getCanonicalDecl() == derived->getCanonicalDecl() ||
      !derived->isDerivedFrom(base))
    return;
  diag(ce->getBeginLoc(), "copying %0 into its polymorphic base %1 slices off the derived part") << derived << base;
  howToFix(ce->getBeginLoc(), "pass or store by reference, pointer-like view or owning smart pointer, or give the "
                              "base a virtual clone() and delete its copy constructor");
}

} // namespace reloco::tidy

namespace reloco::tidy {

// reloco-virtual-default-argument: default arguments are bound statically, so overrides never see their own.
void VirtualDefaultArgumentCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxMethodDecl(isVirtual(), unless(isImplicit()), unless(isTemplateInstantiation())).bind("method"),
                     this);
}

void VirtualDefaultArgumentCheck::check(const MatchFinder::MatchResult &result) {
  const auto *md = result.Nodes.getNodeAs<CXXMethodDecl>("method");
  if (!md || md->isInvalidDecl() || exempt(*result.SourceManager, md->getLocation()) || inUnsafeTier(md))
    return;
  for (const ParmVarDecl *p : md->parameters()) {
    if (!p->hasDefaultArg() || p->hasUninstantiatedDefaultArg() || p->getBeginLoc().isMacroID())
      continue;
    diag(p->getLocation(), "virtual function %0 has a default argument for %1; defaults are bound to the static type, "
                           "so an override called through a base reference sees a different value")
        << md << p;
    howToFix(p->getLocation(), "remove the default and add a non-virtual overload that forwards the default value");
    return;
  }
}

} // namespace reloco::tidy
