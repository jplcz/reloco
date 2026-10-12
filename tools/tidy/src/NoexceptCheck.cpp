// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/Attr.h"

namespace reloco::tidy {

void NoexceptCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(unless(isImplicit()), unless(isDeleted()), unless(isDefaulted()),
                                  unless(cxxDestructorDecl()), unless(isMain()))
                         .bind("fn"),
                     this);
}

void NoexceptCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || fn->getCanonicalDecl() != fn || fn->isTemplateInstantiation() || fn->isExternC() ||
      exempt(*result.SourceManager, fn->getLocation()))
    return;
  if (const auto *md = llvm::dyn_cast<CXXMethodDecl>(fn); md && (md->getParent()->isLambda() || md->isLambdaStaticInvoker()))
    return;

  const auto *proto = fn->getType()->getAs<FunctionProtoType>();
  if (!proto)
    return;
  // Dependent noexcept(expr) specifications are conditional and therefore explicit.
  if (proto->getExceptionSpecType() == EST_None || proto->getExceptionSpecType() == EST_Dynamic)
  {
    auto d = diag(fn->getLocation(), "function %0 must be declared noexcept; add 'noexcept' (or 'noexcept(expr)' when "
                                     "it is conditional) and report failure through a result type")
             << fn;
    if (const SourceLocation at = functionQualifierEnd(fn, *result.SourceManager, result.Context->getLangOpts(), true);
        at.isValid())
      d << FixItHint::CreateInsertion(at, " noexcept");
  }
}

} // namespace reloco::tidy
