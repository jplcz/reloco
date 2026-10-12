// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-lock-guard: a class that owns a mutex (a capability) must say what the mutex protects. Access to members
// annotated RELOCO_GUARDED_BY is then enforced by Clang's -Wthread-safety, which this check complements.

#include "Checks.h"

#include "clang/AST/Attr.h"
#include "clang/Lex/Lexer.h"

#include <vector>

namespace reloco::tidy {

namespace {

bool isCapability(QualType t) {
  const CXXRecordDecl *rd = t.getNonReferenceType()->getAsCXXRecordDecl();
  if (!rd)
    return false;
  if (rd->hasAttr<CapabilityAttr>())
    return true;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
    return spec->getSpecializedTemplate()->getTemplatedDecl()->hasAttr<CapabilityAttr>();
  return false;
}

bool hasAnnotation(const Decl *d, llvm::StringRef name) {
  for (const auto *a : d->specific_attrs<AnnotateAttr>())
    if (a->getAnnotation() == name)
      return true;
  return false;
}

// Members that need no guard: atomics and synchronisation primitives guard themselves.
bool selfSynchronised(QualType t) {
  const CXXRecordDecl *rd = t->getAsCXXRecordDecl();
  if (!rd || !rd->getIdentifier())
    return false;
  const llvm::StringRef n = rd->getName();
  return n.contains("atomic") || n.contains("condition_variable") || n.contains("once") || n == "seqlock" ||
         n.contains("barrier") || n.contains("semaphore") || n.contains("latch");
}

} // namespace

void LockGuardCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit()), unless(isTemplateInstantiation()),
                                   unless(isLambda()), has(fieldDecl()))
                         .bind("class"),
                     this);
}

void LockGuardCheck::check(const MatchFinder::MatchResult &result) {
  const auto *rd = result.Nodes.getNodeAs<CXXRecordDecl>("class");
  if (!rd || rd->isInvalidDecl() || rd->isUnion() || exempt(*result.SourceManager, rd->getLocation()))
    return;

  std::vector<const FieldDecl *> mutexes;
  for (const FieldDecl *f : rd->fields())
    if (isCapability(f->getType()))
      mutexes.push_back(f);
  if (mutexes.empty())
    return;

  for (const FieldDecl *f : rd->fields()) {
    const QualType t = f->getType();
    if (isCapability(t) || f->hasAttr<GuardedByAttr>() || f->hasAttr<PtGuardedByAttr>() ||
        hasAnnotation(f, "reloco_unguarded") || t.isConstQualified() || t->isReferenceType() || f->isBitField() ||
        selfSynchronised(t.getNonReferenceType()) || exempt(*result.SourceManager, f->getLocation()))
      continue;
    auto d = diag(f->getLocation(), "member %0 shares class %1 with mutex %2 but is not declared RELOCO_GUARDED_BY(...)")
             << f << rd << mutexes.front();
    howToFix(f->getLocation(), "annotate what the mutex protects ('int count_ RELOCO_GUARDED_BY(mu_);') and enable "
                               "-Wthread-safety so unlocked access is an error; if the member is immutable or "
                               "single-threaded, say so with RELOCO_UNGUARDED or make it const");
    const SourceLocation end = Lexer::getLocForEndOfToken(f->getLocation(), 0, *result.SourceManager, getLangOpts());
    if (mutexes.size() == 1 && end.isValid() && !f->getLocation().isMacroID() && !t->isArrayType())
      d << FixItHint::CreateInsertion(end, " RELOCO_GUARDED_BY(" + mutexes.front()->getName().str() + ")");
  }
}

} // namespace reloco::tidy
