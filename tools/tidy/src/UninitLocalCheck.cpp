// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

namespace reloco::tidy {

void UninitLocalCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(varDecl(hasLocalStorage(), unless(parmVarDecl()),
                             unless(isImplicit()))
                         .bind("var"),
                     this);
}

void UninitLocalCheck::check(const MatchFinder::MatchResult &result) {
  const auto *vd = result.Nodes.getNodeAs<VarDecl>("var");
  if (!vd || vd->isExceptionVariable() || vd->isStaticLocal() || vd->hasExternalStorage() ||
      vd->isInvalidDecl() || exempt(*result.SourceManager, vd->getLocation()))
    return;
  QualType t = vd->getType();
  if (t.isNull() || t->isDependentType())
    return;
  if (vd->hasInit()) {
    // `pod p;` carries an implicit trivial default construction: still uninitialised.
    const auto *ce = llvm::dyn_cast<CXXConstructExpr>(vd->getInit()->IgnoreImplicit());
    if (!ce || !ce->getConstructor()->isDefaultConstructor() || !ce->getConstructor()->isTrivial() ||
        ce->getParenOrBraceRange().isValid() || vd->getInitStyle() != VarDecl::CallInit)
      return;
  }
  t = result.Context->getBaseElementType(t);
  if (const CXXRecordDecl *rd = t->getAsCXXRecordDecl()) {
    // A class with a non-trivial default constructor initialises itself.
    if (rd->hasDefinition() && !rd->hasTrivialDefaultConstructor())
      return;
  }
  diag(vd->getLocation(), "local variable %0 is declared uninitialized; give it a value (e.g. '= {}')") << vd;
}

} // namespace reloco::tidy
