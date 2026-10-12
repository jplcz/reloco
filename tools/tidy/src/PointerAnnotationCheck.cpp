// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/Attr.h"

namespace reloco::tidy {

void PointerAnnotationCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit()), unless(isLambda())).bind("record"), this);
}

void PointerAnnotationCheck::check(const MatchFinder::MatchResult &result) {
  const auto *rd = result.Nodes.getNodeAs<CXXRecordDecl>("record");
  if (!rd || rd->getCanonicalDecl() != rd || rd->isLocalClass() ||
      rd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation || recordIsPointer(rd) || recordIsOwner(rd) ||
      exempt(*result.SourceManager, rd->getLocation()))
    return;
  // `formatter<T>` specializations only hold the parsed format spec for the duration of one format call.
  if (rd->getName() == "formatter")
    return;

  for (const FieldDecl *fd : rd->fields()) {
    const QualType t = fd->getType();
    if (t.isNull() || isStaticStrType(t, *result.Context))
      continue;
    const bool borrowsRaw = (t->isPointerType() && !t->isFunctionPointerType() && !t->isMemberPointerType()) ||
                            t->isReferenceType();
    if (borrowsRaw || (!t->isDependentType() && isPointerLike(t))) {
      diag(rd->getLocation(), "%0 holds borrowed storage in member %1; annotate the class with RELOCO_POINTER "
                              "(borrows) or RELOCO_OWNER (owns)")
          << rd << fd;
      howToFix(rd->getLocation(), "'struct RELOCO_POINTER view { const int *p; };' for a non-owning handle (Clang then tracks "
                                  "dangling), 'struct RELOCO_OWNER box { ... };' for the type that owns the storage");
      return;
    }
  }
}

} // namespace reloco::tidy
