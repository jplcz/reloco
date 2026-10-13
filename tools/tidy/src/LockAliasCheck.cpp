// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-lock-alias: locks may not be reached through pointers. A pointer to a lock (variable, field, parameter) or
// the address of a lock (&lock) creates a second name for the same lock that reloco-lock-balance cannot follow.
// References are allowed (never rebound, so they name the same lock as their initialiser). Lock implementations,
// RAII guards and RELOCO_LOCK_IMPL code are exempt, and so are RELOCO_LEGACY_LOCK types.

#include "Checks.h"
#include "LockModel.h"

namespace reloco::tidy {

void LockAliasCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(varDecl(unless(isImplicit())).bind("decl"), this);
  finder->addMatcher(fieldDecl().bind("decl"), this);
  finder->addMatcher(unaryOperator(hasOperatorName("&")).bind("addr"), this);
}

void LockAliasCheck::check(const MatchFinder::MatchResult &result) {
  const SourceManager &sm = *result.SourceManager;
  if (const auto *d = result.Nodes.getNodeAs<DeclaratorDecl>("decl")) {
    if (d->isInvalidDecl() || exempt(sm, d->getLocation()) || lockmodel::inLockImplementation(d))
      return;
    const QualType t = d->getType();
    if (t.isNull() || t->isDependentType())
      return;
    QualType base = t.getNonReferenceType();
    int ptrs = 0;
    while (base->isPointerType() || base->isArrayType()) {
      if (base->isPointerType()) {
        ++ptrs;
        base = base->getPointeeType();
      } else {
        base = llvm::cast<ArrayType>(base.getCanonicalType().getTypePtr())->getElementType();
      }
    }
    if (ptrs == 0 || !lockmodel::isLockType(base) || lockmodel::isLegacyLock(lockmodel::recordOfType(base)))
      return;
    diag(d->getLocation(), "%0 is a pointer to a lock; a pointer is a second name for the lock that lock tracking "
                           "cannot follow")
        << d;
    howToFix(d->getLocation(), "use a reference ('Lock &l') or reach the lock through its owner ('owner.m_lock.lock()'); "
                               "only lock implementations and guards may hold lock pointers");
    return;
  }
  if (const auto *u = result.Nodes.getNodeAs<UnaryOperator>("addr")) {
    if (exempt(sm, u->getOperatorLoc()) || u->getSubExpr()->isTypeDependent())
      return;
    if (!lockmodel::isLockType(u->getSubExpr()->getType()) || u->getSubExpr()->getType()->isPointerType() ||
        lockmodel::isLegacyLock(lockmodel::recordOfType(u->getSubExpr()->getType())))
      return;
    const auto &parents = result.Context->getParents(*u);
    const Decl *owner = nullptr;
    for (auto p = parents; !p.empty() && !owner;) {
      if (const auto *fd = p[0].get<FunctionDecl>())
        owner = fd;
      else
        p = result.Context->getParents(p[0]);
    }
    if (owner && lockmodel::inLockImplementation(owner))
      return;
    if (owner && lockmodel::isLockImplementation(llvm::dyn_cast_or_null<CXXRecordDecl>(owner->getDeclContext())))
      return;
    diag(u->getOperatorLoc(), "taking the address of a lock creates an alias that lock tracking cannot follow");
    howToFix(u->getOperatorLoc(), "pass the lock by reference ('Lock &') or use it in place");
  }
}

} // namespace reloco::tidy
