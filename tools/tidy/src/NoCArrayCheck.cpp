// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

namespace reloco::tidy {

void NoCArrayCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(varDecl(unless(isImplicit()), unless(parmVarDecl()), unless(isExpansionInSystemHeader())).bind("decl"),
                     this);
  finder->addMatcher(fieldDecl(unless(isImplicit()), unless(isExpansionInSystemHeader())).bind("decl"), this);
}

void NoCArrayCheck::check(const MatchFinder::MatchResult &result) {
  const auto *d = result.Nodes.getNodeAs<DeclaratorDecl>("decl");
  if (!d || d->isInvalidDecl() || !d->getType()->isArrayType() || d->isTemplated() ||
      exempt(*result.SourceManager, d->getLocation()) || inUnsafeTier(d))
    return;
  // `extern "C"` declarations describe foreign ABI and cannot choose their type.
  if (const auto *vd = llvm::dyn_cast<VarDecl>(d); vd && vd->isExternC())
    return;
  diag(d->getLocation(), "raw C array '%0'; use reloco::array, reloco::inline_vector or a span, or suppress with NOLINT where "
                         "constructors cannot run (early boot / embedded)")
      << d->getName();
}

} // namespace reloco::tidy
