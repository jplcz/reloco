// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"


namespace reloco::tidy {

namespace {

bool isInstantiation(const CXXMethodDecl *md) {
  if (md->isTemplateInstantiation() || md->getInstantiatedFromMemberFunction())
    return true;
  const CXXRecordDecl *rd = md->getParent();
  return rd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation || rd->isLambda();
}

bool hasRvalueOverload(const CXXMethodDecl *md) {
  for (const CXXMethodDecl *other : md->getParent()->methods())
    if (other->getDeclName() == md->getDeclName() && other->getRefQualifier() == RQ_RValue &&
        other->isConst() == md->isConst())
      return true;
  return false;
}

} // namespace

void RefQualifiedMemberCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxMethodDecl(unless(isImplicit()), unless(isStaticStorageClass()), unless(cxxConstructorDecl()),
                                   unless(cxxDestructorDecl()), unless(isDeleted()))
                         .bind("method"),
                     this);
}

void RefQualifiedMemberCheck::check(const MatchFinder::MatchResult &result) {
  const auto *md = result.Nodes.getNodeAs<CXXMethodDecl>("method");
  if (!md || md->getCanonicalDecl() != md || isInstantiation(md) || md->isVirtual() || md->size_overridden_methods() ||
      md->isCopyAssignmentOperator() || md->isMoveAssignmentOperator() || exempt(*result.SourceManager, md->getLocation()))
    return;

  const CXXRecordDecl *parent = md->getParent();
  const bool aliasing = isAliasing(md->getReturnType());
  const bool handle = recordIsPointer(parent);

  if (md->isConst()) {
    // Non-aliasing const getters return values, which stay valid on temporaries.
    if (!aliasing || handle)
      return;
    if (md->getRefQualifier() == RQ_None) {
      auto d = diag(md->getLocation(), "const member %0 returns an alias of its object but is not ref-qualified; "
                                       "declare it 'const &' and delete the 'const &&' overload")
               << md;
      if (const SourceLocation at = functionQualifierEnd(md, *result.SourceManager, result.Context->getLangOpts(), false); at.isValid())
        d << FixItHint::CreateInsertion(at, " &");
    }
    else if (md->getRefQualifier() == RQ_LValue && !hasRvalueOverload(md))
      diag(md->getLocation(), "const & member %0 returns an alias of its object and still binds temporaries; "
                              "add a deleted 'const &&' overload (RELOCO_BLOCK_RVALUE_ACCESS)")
          << md;
    return;
  }

  // A non-const member that returns no alias cannot leave a reference into a dying temporary behind.
  if (!aliasing && !strict_)
    return;
  if (md->getRefQualifier() == RQ_None) {
    auto d = diag(md->getLocation(), "non-const member %0 is callable on temporaries; qualify it with '&'") << md;
    if (const SourceLocation at = functionQualifierEnd(md, *result.SourceManager, result.Context->getLangOpts(), false); at.isValid())
      d << FixItHint::CreateInsertion(at, " &");
  }
}

} // namespace reloco::tidy
