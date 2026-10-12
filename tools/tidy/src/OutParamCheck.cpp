// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/Attr.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/BitVector.h"

#include <vector>

namespace reloco::tidy {

namespace {

bool isOut(const ParmVarDecl *p) {
  for (const auto *a : p->specific_attrs<AnnotateAttr>())
    if (a->getAnnotation() == "reloco_out")
      return true;
  return false;
}

const Expr *strip(const Expr *e) { return e ? e->IgnoreParenImpCasts() : nullptr; }

// Is `e` the out-parameter itself: `*p`, `p[i]`, `p->m` (pointer) or `r`, `r.m` (reference)?
bool isPlaceOf(const Expr *e, const ParmVarDecl *p) {
  e = strip(e);
  if (!e)
    return false;
  if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e))
    return dre->getDecl() == p && p->getType()->isReferenceType();
  if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e))
    return uo->getOpcode() == UO_Deref && llvm::isa_and_nonnull<DeclRefExpr>(strip(uo->getSubExpr())) &&
           llvm::cast<DeclRefExpr>(strip(uo->getSubExpr()))->getDecl() == p;
  if (const auto *as = llvm::dyn_cast<ArraySubscriptExpr>(e))
    return isPlaceOf(as->getBase(), p) || (llvm::isa<DeclRefExpr>(strip(as->getBase())) &&
                                           llvm::cast<DeclRefExpr>(strip(as->getBase()))->getDecl() == p);
  if (const auto *me = llvm::dyn_cast<MemberExpr>(e)) {
    const Expr *b = strip(me->getBase());
    if (me->isArrow())
      return llvm::isa<DeclRefExpr>(b) && llvm::cast<DeclRefExpr>(b)->getDecl() == p;
    return isPlaceOf(b, p);
  }
  return false;
}

bool isPtrValue(const Expr *e, const ParmVarDecl *p) {
  const auto *dre = llvm::dyn_cast_or_null<DeclRefExpr>(strip(e));
  return dre && dre->getDecl() == p;
}

// Does this CFG element write the out-parameter?
bool writes(const Stmt *s, const ParmVarDecl *p) {
  if (const auto *bo = llvm::dyn_cast<BinaryOperator>(s))
    return bo->isAssignmentOp() && isPlaceOf(bo->getLHS(), p);
  if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(s)) {
    if (oc->isAssignmentOp() && oc->getNumArgs() > 0 && isPlaceOf(oc->getArg(0), p))
      return true;
  }
  if (const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(s)) {
    if (const auto *md = mc->getMethodDecl(); md && !md->isConst() && isPlaceOf(mc->getImplicitObjectArgument(), p))
      return true;
  }
  if (const auto *call = llvm::dyn_cast<CallExpr>(s)) {
    const FunctionDecl *fd = call->getDirectCallee();
    for (unsigned i = 0; fd && i < call->getNumArgs() && i < fd->getNumParams(); ++i) {
      const QualType pt = fd->getParamDecl(i)->getType();
      const bool mutRef = pt->isReferenceType() && !pt.getNonReferenceType().isConstQualified();
      const bool mutPtr = pt->isPointerType() && !pt->getPointeeType().isConstQualified();
      if ((mutRef && isPlaceOf(call->getArg(i), p)) || (mutPtr && isPtrValue(call->getArg(i), p)))
        return true;
    }
  }
  if (const auto *ce = llvm::dyn_cast<CXXConstructExpr>(s)) {
    // `T v = *x;` copies from it (a read), `foo(x)` style handoff goes through CallExpr above.
    (void)ce;
  }
  return false;
}

bool reads(const Stmt *s, const ParmVarDecl *p) {
  const auto *ic = llvm::dyn_cast<ImplicitCastExpr>(s);
  return ic && ic->getCastKind() == CK_LValueToRValue && isPlaceOf(ic->getSubExpr(), p);
}

} // namespace

void OutParamCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit()), unless(isTemplateInstantiation()),
                                  hasAnyParameter(parmVarDecl().bind("probe")))
                         .bind("fn"),
                     this);
}

void OutParamCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || fn->isInvalidDecl() || exempt(*result.SourceManager, fn->getLocation()))
    return;

  std::vector<const ParmVarDecl *> outs;
  for (const ParmVarDecl *p : fn->parameters()) {
    if (!isOut(p))
      continue;
    const QualType t = p->getType();
    const bool ok = (t->isPointerType() && !t->getPointeeType().isConstQualified()) ||
                    (t->isLValueReferenceType() && !t.getNonReferenceType().isConstQualified());
    if (!ok) {
      diag(p->getLocation(), "RELOCO_OUT requires a pointer or lvalue reference to non-const; %0 is neither") << p;
      howToFix(p->getLocation(), "'void get(int *x RELOCO_OUT)' or 'void get(int &x RELOCO_OUT)'; read-only inputs stay const and "
                                 "unmarked");
      continue;
    }
    outs.push_back(p);
  }
  if (outs.empty())
    return;

  CFG::BuildOptions opts;
  opts.setAllAlwaysAdd();
  const std::unique_ptr<CFG> cfg = CFG::buildCFG(fn, fn->getBody(), result.Context, opts);
  if (!cfg)
    return;

  for (const ParmVarDecl *p : outs) {
    const unsigned n = cfg->getNumBlockIDs();
    // Must-analysis: out[b] is true when every path reaching the end of block b has written the parameter.
    std::vector<char> in(n, 1), out(n, 1), seen(n, 0);
    bool changed = true;
    std::vector<const CFGBlock *> blocks;
    for (const CFGBlock *b : *cfg)
      blocks.push_back(b);
    while (changed) {
      changed = false;
      for (const CFGBlock *b : blocks) {
        const unsigned id = b->getBlockID();
        char i = b == &cfg->getEntry() ? 0 : 1;
        bool anyPred = false;
        for (const CFGBlock::AdjacentBlock &pred : b->preds())
          if (pred.isReachable()) {
            anyPred = true;
            i = i && out[pred->getBlockID()];
          }
        if (!anyPred && b != &cfg->getEntry())
          i = 1;
        char o = i;
        for (const CFGElement &el : *b)
          if (auto cs = el.getAs<CFGStmt>(); cs && writes(cs->getStmt(), p))
            o = 1;
        if (in[id] != i || out[id] != o || !seen[id]) {
          in[id] = i;
          out[id] = o;
          seen[id] = 1;
          changed = true;
        }
      }
    }

    // Reads before the first write, walking each block with its own entry state.
    for (const CFGBlock *b : blocks) {
      bool w = in[b->getBlockID()];
      for (const CFGElement &el : *b) {
        const auto cs = el.getAs<CFGStmt>();
        if (!cs)
          continue;
        if (!w && reads(cs->getStmt(), p)) {
          diag(cs->getStmt()->getBeginLoc(), "out-parameter %0 is read before it has been written") << p;
          howToFix(cs->getStmt()->getBeginLoc(), "assign first ('*x = initial;'), or take the value as a separate input "
                                                 "parameter instead of an out-parameter");
        }
        if (writes(cs->getStmt(), p))
          w = true;
      }
    }

    // Every edge into Exit must carry a written state.
    for (const CFGBlock::AdjacentBlock &pred : cfg->getExit().preds()) {
      if (!pred.isReachable() || pred->hasNoReturnElement())
        continue;
      if (out[pred->getBlockID()])
        continue;
      SourceLocation loc = fn->getBody()->getEndLoc();
      for (const CFGElement &el : *pred)
        if (auto cs = el.getAs<CFGStmt>(); cs && llvm::isa<ReturnStmt>(cs->getStmt()))
          loc = cs->getStmt()->getBeginLoc();
      diag(loc, "out-parameter %0 is not written on every path that returns") << p;
      howToFix(loc, "write it before each return, e.g. '*x = 0;' at the top of the function, then overwrite later; for "
                    "'T &x RELOCO_OUT' assign 'x = value;'");
    }
  }
}

} // namespace reloco::tidy
