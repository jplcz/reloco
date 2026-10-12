// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-iterator-invalidation: a range-for holds hidden iterators into its range; structurally modifying the range
// inside the body invalidates them. Iterators/references bound to named variables are covered by reloco-borrow-check.

#include "Checks.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include <vector>

namespace reloco::tidy {

namespace {

using Chain = std::vector<const ValueDecl *>;

// `v`, `this->v`, `obj.v` -> the declarations naming the place, outermost last.
bool chainOf(const Expr *e, Chain &out) {
  e = e->IgnoreParenImpCasts();
  if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e)) {
    out.push_back(dre->getDecl());
    return true;
  }
  if (const auto *me = llvm::dyn_cast<MemberExpr>(e)) {
    if (!llvm::isa<CXXThisExpr>(me->getBase()->IgnoreParenImpCasts()) && !chainOf(me->getBase(), out))
      return false;
    out.push_back(me->getMemberDecl());
    return true;
  }
  return false;
}

bool sameChain(const Expr *e, const Chain &c) {
  Chain mine;
  return chainOf(e, mine) && mine == c;
}

// Accessors that never reallocate or reorder, even when non-const.
bool preservesIterators(llvm::StringRef n) {
  static const char *const names[] = {"at",   "front", "back", "data",     "begin", "end",   "rbegin", "rend",
                                      "find", "get",   "size", "capacity", "empty", "count", "contains"};
  for (const char *s : names)
    if (n == s)
      return true;
  return false;
}

class BodyVisitor : public RecursiveASTVisitor<BodyVisitor> {
public:
  BodyVisitor(ASTContext &ctx, const Chain &range, std::vector<const Expr *> &hits)
      : ctx_(ctx), range_(range), hits_(hits) {}

  bool VisitCXXMemberCallExpr(CXXMemberCallExpr *mc) {
    const CXXMethodDecl *md = mc->getMethodDecl();
    if (!md || md->isConst() || (md->getIdentifier() && preservesIterators(md->getName())))
      return true;
    if (sameChain(mc->getImplicitObjectArgument(), range_))
      note(mc);
    return true;
  }

  bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *oc) {
    const auto *md = llvm::dyn_cast_or_null<CXXMethodDecl>(oc->getDirectCallee());
    if (oc->getNumArgs() && md && !md->isConst() && oc->getOperator() != OO_Subscript && oc->getOperator() != OO_Star &&
        oc->getOperator() != OO_Arrow && sameChain(oc->getArg(0), range_))
      note(oc);
    return true;
  }

  // `helper(v)` / `helper(&v)` with a mutable view of the range.
  bool VisitCallExpr(CallExpr *call) {
    if (llvm::isa<CXXMemberCallExpr>(call) || llvm::isa<CXXOperatorCallExpr>(call))
      return true;
    const FunctionDecl *fd = call->getDirectCallee();
    for (unsigned i = 0; fd && i < call->getNumArgs() && i < fd->getNumParams(); ++i) {
      const QualType pt = fd->getParamDecl(i)->getType();
      const bool mutRef = pt->isLValueReferenceType() && !pt.getNonReferenceType().isConstQualified();
      if (mutRef && sameChain(call->getArg(i), range_))
        note(call);
    }
    return true;
  }

private:
  // `v.erase(it); break;` and `v.push_back(x); return;` stop iterating, so the stale iterators are never used.
  bool endsLoop(const Expr *e) {
    const Stmt *cur = e;
    while (cur) {
      const Stmt *parent = nullptr;
      for (const DynTypedNode &p : ctx_.getParents(*cur))
        parent = p.get<Stmt>();
      if (!parent)
        return false;
      if (const auto *cs = llvm::dyn_cast<CompoundStmt>(parent)) {
        for (auto it = cs->body_begin(); it != cs->body_end(); ++it)
          if (*it == cur)
            return std::next(it) != cs->body_end() && llvm::isa<BreakStmt, ReturnStmt>(*std::next(it));
        return false;
      }
      if (llvm::isa<CXXForRangeStmt, ForStmt, WhileStmt, DoStmt>(parent))
        return false;
      cur = parent;
    }
    return false;
  }

  void note(const Expr *e) {
    if (!endsLoop(e))
      hits_.push_back(e);
  }

  ASTContext &ctx_;
  const Chain &range_;
  std::vector<const Expr *> &hits_;
};

} // namespace

void IteratorInvalidationCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxForRangeStmt(unless(isInTemplateInstantiation())).bind("loop"), this);
}

void IteratorInvalidationCheck::check(const MatchFinder::MatchResult &result) {
  const auto *loop = result.Nodes.getNodeAs<CXXForRangeStmt>("loop");
  if (!loop || !loop->getBody() || exempt(*result.SourceManager, loop->getForLoc()))
    return;
  const Expr *range = loop->getRangeInit();
  if (!range || range->getType().getNonReferenceType().isConstQualified())
    return;
  Chain chain;
  if (!chainOf(range, chain))
    return;

  std::vector<const Expr *> hits;
  BodyVisitor v(*result.Context, chain, hits);
  v.TraverseStmt(const_cast<Stmt *>(loop->getBody()));
  for (const Expr *hit : hits) {
    if (exempt(*result.SourceManager, hit->getBeginLoc()))
      continue;
    diag(hit->getBeginLoc(), "the range being iterated is modified inside the range-for body; its hidden iterators may be "
                             "invalidated");
    diag(loop->getForLoc(), "iterating here", DiagnosticIDs::Note);
    howToFix(hit->getBeginLoc(), "collect the changes and apply them after the loop, iterate over an explicit "
                                 "copy of the container, use an index loop with explicit bounds, or "
                                 "stop iterating right after the change ('v.erase(it); break;')");
  }
}

} // namespace reloco::tidy
