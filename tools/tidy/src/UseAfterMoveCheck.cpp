// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/Analysis/CFG.h"

#include <map>
#include <set>
#include <vector>

namespace reloco::tidy {

namespace {

using MovedSet = std::map<const VarDecl *, SourceLocation>;

bool isStdMove(const CallExpr *call) {
  const FunctionDecl *fd = call->getDirectCallee();
  if (!fd || !fd->getIdentifier() || fd->getName() != "move" || call->getNumArgs() != 1)
    return false;
  const auto *ns = llvm::dyn_cast<NamespaceDecl>(fd->getDeclContext()->getRedeclContext());
  return ns && ns->getIdentifier() && ns->getName() == "std";
}

// The tracked variable moved by `std::move(x)`, or null when the move cannot leave x in a moved-from state.
const VarDecl *movedVar(const CallExpr *call) {
  const auto *dre = llvm::dyn_cast<DeclRefExpr>(call->getArg(0)->IgnoreParenImpCasts());
  const auto *vd = dre ? llvm::dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
  if (!vd || vd->isStaticLocal() || vd->hasGlobalStorage() || vd->getType()->isScalarType() ||
      vd->getType().getNonReferenceType().isTriviallyCopyableType(vd->getASTContext()))
    return nullptr;
  return vd;
}

bool isReinitName(llvm::StringRef n) {
  return n == "clear" || n == "reset" || n == "assign" || n == "emplace" || n == "emplace_back" || n == "reinit" ||
         n == "init";
}

class Analysis {
public:
  Analysis(ASTContext &ctx) : ctx_(ctx) {}

  // `x = ...` (builtin or overloaded), the variable is valid again afterwards.
  static const VarDecl *assignedVar(const Stmt *s) {
    const Expr *lhs = nullptr;
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(s); bo && bo->isAssignmentOp())
      lhs = bo->getLHS();
    else if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(s); oc && oc->isAssignmentOp() && oc->getNumArgs())
      lhs = oc->getArg(0);
    if (!lhs)
      return nullptr;
    const auto *dre = llvm::dyn_cast<DeclRefExpr>(lhs->IgnoreParenImpCasts());
    return dre ? llvm::dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
  }

  // `x.clear()` and friends re-establish a known state.
  static const VarDecl *reinitVar(const Stmt *s) {
    const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(s);
    if (!mc || !mc->getMethodDecl() || !mc->getMethodDecl()->getIdentifier() ||
        !isReinitName(mc->getMethodDecl()->getName()))
      return nullptr;
    const auto *dre = llvm::dyn_cast_or_null<DeclRefExpr>(mc->getImplicitObjectArgument()->IgnoreParenImpCasts());
    return dre ? llvm::dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
  }

  // The DeclRefExpr is only a target being (re)initialised, not a use of the old value.
  bool isReinitTarget(const DeclRefExpr *dre) {
    for (const DynTypedNode &p : ctx_.getParents(*dre)) {
      const Stmt *s = p.get<Stmt>();
      if (!s)
        continue;
      if (const auto *bo = llvm::dyn_cast<BinaryOperator>(s); bo && bo->isAssignmentOp() && bo->getLHS()->IgnoreParenImpCasts() == dre)
        return true;
      if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(s);
          oc && oc->isAssignmentOp() && oc->getNumArgs() && oc->getArg(0)->IgnoreParenImpCasts() == dre)
        return true;
      if (const auto *me = llvm::dyn_cast<MemberExpr>(s)) {
        for (const DynTypedNode &pp : ctx_.getParents(*me))
          if (const auto *mc = pp.get<CXXMemberCallExpr>(); mc && mc->getMethodDecl() &&
                                                           mc->getMethodDecl()->getIdentifier() &&
                                                           isReinitName(mc->getMethodDecl()->getName()))
            return true;
      }
    }
    return false;
  }

  // Applies one CFG element; reports uses of moved variables when `report` is set.
  template <typename Report> void step(const Stmt *s, MovedSet &moved, Report &&report) {
    if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(s)) {
      const auto *vd = llvm::dyn_cast<VarDecl>(dre->getDecl());
      if (!vd)
        return;
      const auto it = moved.find(vd);
      if (it != moved.end() && !isReinitTarget(dre))
        report(dre, vd, it->second);
      return;
    }
    if (const auto *call = llvm::dyn_cast<CallExpr>(s); call && isStdMove(call)) {
      if (const VarDecl *vd = movedVar(call))
        moved[vd] = call->getBeginLoc();
      return;
    }
    if (const VarDecl *vd = assignedVar(s))
      moved.erase(vd);
    else if (const VarDecl *rv = reinitVar(s))
      moved.erase(rv);
  }

private:
  ASTContext &ctx_;
};

} // namespace

void UseAfterMoveCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit()), unless(isTemplateInstantiation()),
                                  forEachDescendant(callExpr(callee(functionDecl(hasName("::std::move"))))))
                         .bind("fn"),
                     this);
}

void UseAfterMoveCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || fn->isInvalidDecl() || exempt(*result.SourceManager, fn->getLocation()))
    return;
  if (!seen_.insert(fn->getCanonicalDecl()).second)
    return;

  CFG::BuildOptions opts;
  opts.setAllAlwaysAdd();
  const std::unique_ptr<CFG> cfg = CFG::buildCFG(fn, fn->getBody(), result.Context, opts);
  if (!cfg)
    return;

  Analysis an(*result.Context);
  std::vector<const CFGBlock *> blocks;
  for (const CFGBlock *b : *cfg)
    blocks.push_back(b);

  // May-analysis: a variable is "moved" at a point if some path to it moved the variable and did not reinitialise it.
  std::vector<MovedSet> in(cfg->getNumBlockIDs()), out(cfg->getNumBlockIDs());
  for (bool changed = true; changed;) {
    changed = false;
    for (const CFGBlock *b : blocks) {
      MovedSet s;
      for (const CFGBlock::AdjacentBlock &pred : b->preds())
        if (pred.isReachable())
          for (const auto &kv : out[pred->getBlockID()])
            s.emplace(kv);
      in[b->getBlockID()] = s;
      for (const CFGElement &el : *b)
        if (auto cs = el.getAs<CFGStmt>())
          an.step(cs->getStmt(), s, [](const DeclRefExpr *, const VarDecl *, SourceLocation) {});
      if (s != out[b->getBlockID()]) {
        out[b->getBlockID()] = std::move(s);
        changed = true;
      }
    }
  }

  std::set<const DeclRefExpr *> reported;
  for (const CFGBlock *b : blocks) {
    MovedSet s = in[b->getBlockID()];
    for (const CFGElement &el : *b)
      if (auto cs = el.getAs<CFGStmt>())
        an.step(cs->getStmt(), s, [&](const DeclRefExpr *use, const VarDecl *vd, SourceLocation movedAt) {
          if (!reported.insert(use).second || exempt(*result.SourceManager, use->getBeginLoc()))
            return;
          diag(use->getBeginLoc(), "%0 is used after it was moved from") << vd;
          diag(movedAt, "moved here", DiagnosticIDs::Note);
          howToFix(use->getBeginLoc(), "reassign it ('x = T{};') or call a reinitialising member ('x.clear();') before "
                                       "reuse, copy explicitly ('T copy(x);') instead of moving, or restructure so the "
                                       "moved-from value is never touched again (Rust rejects this at compile time)");
        });
  }
}

} // namespace reloco::tidy
