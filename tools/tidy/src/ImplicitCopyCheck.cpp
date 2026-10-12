// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"

namespace reloco::tidy {

namespace {

bool hasCopyableMarker(const CXXRecordDecl *rd) {
  const auto marked = [](const Decl *d) {
    for (const auto *a : d->specific_attrs<AnnotateAttr>())
      if (a->getAnnotation() == "reloco_copyable")
        return true;
    return false;
  };
  if (marked(rd))
    return true;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
    return marked(spec->getSpecializedTemplate()->getTemplatedDecl());
  return false;
}

bool isWrapper(const Stmt *s) {
  return llvm::isa<ExprWithCleanups>(s) || llvm::isa<CXXBindTemporaryExpr>(s) ||
         llvm::isa<MaterializeTemporaryExpr>(s) || llvm::isa<ImplicitCastExpr>(s) || llvm::isa<ConstantExpr>(s);
}

} // namespace

void ImplicitCopyCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxConstructExpr(hasDeclaration(cxxConstructorDecl(isCopyConstructor())),
                                      unless(hasAncestor(functionDecl(isImplicit()))))
                         .bind("ctor"),
                     this);
  finder->addMatcher(cxxOperatorCallExpr(isAssignmentOperator(),
                                         callee(cxxMethodDecl(isCopyAssignmentOperator())),
                                         unless(hasAncestor(functionDecl(isImplicit()))))
                         .bind("assign"),
                     this);
}

void ImplicitCopyCheck::check(const MatchFinder::MatchResult &result) {
  const SourceManager &sm = *result.SourceManager;
  const CXXRecordDecl *rd = nullptr;
  SourceLocation loc;

  if (const auto *ce = result.Nodes.getNodeAs<CXXConstructExpr>("ctor")) {
    rd = ce->getConstructor()->getParent();
    loc = ce->getBeginLoc();
    if (ce->isElidable() || ce->isListInitialization())
      return;
    // Climb to the syntactic context to tell `T y = x` (implicit) from `T y(x)` / `T(x)` (explicit clone).
    DynTypedNode cur = DynTypedNode::create(*ce);
    for (;;) {
      auto parents = result.Context->getParents(cur);
      if (parents.empty())
        break;
      cur = parents[0];
      if (const auto *st = cur.get<Stmt>(); st && isWrapper(st))
        continue;
      break;
    }
    // A member/base initialiser (`: m(o)`) hangs directly off the constructor decl: a direct-init, i.e. explicit.
    if (cur.get<CXXConstructExpr>() || cur.get<CXXConstructorDecl>())
      return;
    if (const auto *vd = cur.get<VarDecl>(); vd && vd->getInitStyle() != VarDecl::CInit)
      return;
    if (cur.get<CXXFunctionalCastExpr>() || cur.get<CXXStaticCastExpr>() || cur.get<CXXCtorInitializer>())
      return;
  } else if (const auto *oc = result.Nodes.getNodeAs<CXXOperatorCallExpr>("assign")) {
    rd = llvm::cast<CXXMethodDecl>(oc->getDirectCallee())->getParent();
    loc = oc->getOperatorLoc();
  }

  if (!rd || rd->isLambda() || exempt(sm, loc) || sm.isInSystemHeader(rd->getLocation()) ||
      hasCopyableMarker(rd) || recordIsPointer(rd))
    return;
  diag(loc, "implicit copy of '%0'; move it, copy explicitly with '%0(x)', or mark the type RELOCO_COPYABLE")
      << rd->getName();
  howToFix(loc, "move it ('take(std::move(x))' or return by value), clone on purpose ('T copy(x);'), or, for small "
                "plain-data types, declare 'struct RELOCO_COPYABLE T { ... };'");
}

} // namespace reloco::tidy
