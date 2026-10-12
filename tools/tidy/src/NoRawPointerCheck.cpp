// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

namespace reloco::tidy {

namespace {

bool isVtableLike(QualType pointee) {
  const CXXRecordDecl *rd = pointee.getCanonicalType()->getAsCXXRecordDecl();
  if (!rd || !rd->getIdentifier())
    return false;
  const llvm::StringRef n = rd->getName();
  return n.contains("vtable") || n.contains("vtbl");
}

// The class whose member (field, parameter, return value) is being declared.
const CXXRecordDecl *enclosingClass(const DeclContext *dc) {
  for (; dc; dc = dc->getParent())
    if (const auto *rd = llvm::dyn_cast<CXXRecordDecl>(dc))
      if (!rd->isLambda())
        return rd;
  return nullptr;
}

} // namespace

void NoRawPointerCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(fieldDecl(unless(isImplicit())).bind("decl"), this);
  finder->addMatcher(varDecl(unless(isImplicit()), unless(parmVarDecl())).bind("decl"), this);
  finder->addMatcher(parmVarDecl(unless(isImplicit())).bind("decl"), this);
  finder->addMatcher(functionDecl(unless(isImplicit()), unless(isDeleted())).bind("fn"), this);
}

void NoRawPointerCheck::check(const MatchFinder::MatchResult &result) {
  const ValueDecl *decl = result.Nodes.getNodeAs<ValueDecl>("decl");
  QualType type;
  SourceLocation loc;
  const Decl *anchor = nullptr;
  llvm::StringRef what;

  if (const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn")) {
    if (fn->getCanonicalDecl() != fn || fn->isTemplateInstantiation() || fn->isMain() ||
        llvm::isa<CXXConstructorDecl>(fn) || llvm::isa<CXXDestructorDecl>(fn))
      return;
    type = fn->getReturnType();
    loc = fn->getLocation();
    anchor = fn;
    what = "return type";
  } else if (decl) {
    type = decl->getType();
    loc = decl->getLocation();
    anchor = decl;
    what = llvm::isa<FieldDecl>(decl) ? "member" : llvm::isa<ParmVarDecl>(decl) ? "parameter" : "variable";
  } else {
    return;
  }

  if (type.isNull() || type->isDependentType() || !type->isPointerType() || type->isFunctionPointerType() ||
      type->isMemberPointerType())
    return;
  if (exempt(*result.SourceManager, loc) || inUnsafeTier(anchor) || isVtableLike(type->getPointeeType()))
    return;
  if (const auto *fd = llvm::dyn_cast<FunctionDecl>(anchor); fd && fd->isExternC())
    return;
  if (const auto *vd = llvm::dyn_cast<VarDecl>(anchor); vd && vd->getType()->isPointerType() && vd->isExternC())
    return;

  // Raw pointers are the implementation detail of the borrowing/owning types.
  if (const CXXRecordDecl *rd = enclosingClass(anchor->getDeclContext()))
    if (recordIsPointer(rd) || recordIsOwner(rd))
      return;
  if (const auto *pv = llvm::dyn_cast<ParmVarDecl>(anchor))
    if (const auto *fd = llvm::dyn_cast_or_null<FunctionDecl>(pv->getDeclContext()); fd && fd->isExternC())
      return;

  diag(loc, "raw pointer %0; use reloco::value_ptr/value_ref/span, or confine it to a gsl::pointer/gsl::owner type "
            "or an unsafe_* function")
      << what;
  howToFix(loc, "take 'reloco::span<T>' for a range, 'reloco::value_ref<T>' for a required object, 'reloco::value_ptr<T>' for an "
                "optional one: 'void fill(int *p, size_t n)' becomes 'void fill(reloco::span<int> out)'");
}

} // namespace reloco::tidy
