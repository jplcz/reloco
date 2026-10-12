// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-leak: values of RELOCO_MUST_CONSUME classes (linear resources) may not be silently dropped.

#include "Checks.h"

#include "clang/AST/Attr.h"
#include "clang/Analysis/CFG.h"

#include <map>
#include <set>
#include <vector>

namespace reloco::tidy {

namespace {

bool hasMarker(const Decl *d, llvm::StringRef name) {
  for (const Decl *r : d->redecls())
    for (const auto *a : r->specific_attrs<AnnotateAttr>())
      if (a->getAnnotation() == name)
        return true;
  return false;
}

bool isMustConsume(QualType t) {
  const CXXRecordDecl *rd = t.getNonReferenceType()->getAsCXXRecordDecl();
  if (!rd)
    return false;
  if (hasMarker(rd, "reloco_must_consume"))
    return true;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
    return hasMarker(spec->getSpecializedTemplate()->getTemplatedDecl(), "reloco_must_consume");
  return false;
}

// Tracked variables are owned values, not references.
const VarDecl *trackedVar(const Expr *e) {
  const auto *dre = llvm::dyn_cast_or_null<DeclRefExpr>(e ? e->IgnoreParenImpCasts() : nullptr);
  const auto *vd = dre ? llvm::dyn_cast<VarDecl>(dre->getDecl()) : nullptr;
  return vd && !vd->getType()->isReferenceType() && !vd->hasGlobalStorage() && isMustConsume(vd->getType()) ? vd : nullptr;
}

bool isStdMove(const CallExpr *call) {
  const FunctionDecl *fd = call->getDirectCallee();
  if (!fd || !fd->getIdentifier() || fd->getName() != "move" || call->getNumArgs() != 1)
    return false;
  const auto *ns = llvm::dyn_cast<NamespaceDecl>(fd->getDeclContext()->getRedeclContext());
  return ns && ns->getIdentifier() && ns->getName() == "std";
}

using Live = std::map<const VarDecl *, SourceLocation>;

// Applies one CFG element to the set of live (unconsumed) resources.
void step(const Stmt *s, Live &live) {
  if (const auto *ds = llvm::dyn_cast<DeclStmt>(s)) {
    for (const Decl *d : ds->decls())
      if (const auto *vd = llvm::dyn_cast<VarDecl>(d); vd && !vd->isStaticLocal() && !vd->getType()->isReferenceType() &&
                                                         isMustConsume(vd->getType()))
        live[vd] = vd->getLocation();
    return;
  }
  if (const auto *call = llvm::dyn_cast<CallExpr>(s); call && isStdMove(call)) {
    if (const VarDecl *vd = trackedVar(call->getArg(0)))
      live.erase(vd);
    return;
  }
  if (const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(s)) {
    if (const CXXMethodDecl *md = mc->getMethodDecl(); md && hasMarker(md, "reloco_consumes"))
      if (const VarDecl *vd = trackedVar(mc->getImplicitObjectArgument()))
        live.erase(vd);
    return;
  }
  if (const auto *uo = llvm::dyn_cast<UnaryOperator>(s); uo && uo->getOpcode() == UO_AddrOf) {
    if (const VarDecl *vd = trackedVar(uo->getSubExpr()))
      live.erase(vd);
    return;
  }
  if (const auto *rs = llvm::dyn_cast<ReturnStmt>(s)) {
    // `return t;` wraps t in a move/copy construction and cleanups.
    const Expr *e = rs->getRetValue();
    while (e) {
      e = e->IgnoreParenImpCasts();
      if (const auto *ewc = llvm::dyn_cast<ExprWithCleanups>(e))
        e = ewc->getSubExpr();
      else if (const auto *cc = llvm::dyn_cast<CXXConstructExpr>(e); cc && cc->getNumArgs() == 1)
        e = cc->getArg(0);
      else
        break;
    }
    if (const VarDecl *vd = trackedVar(e))
      live.erase(vd);
  }
}

} // namespace

void LeakCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit()), unless(isTemplateInstantiation())).bind("fn"), this);
}

void LeakCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || fn->isInvalidDecl() || exempt(*result.SourceManager, fn->getLocation()))
    return;

  Live entry;
  for (const ParmVarDecl *p : fn->parameters())
    if (!p->getType()->isReferenceType() && isMustConsume(p->getType()))
      entry[p] = p->getLocation();

  CFG::BuildOptions opts;
  opts.setAllAlwaysAdd();
  const std::unique_ptr<CFG> cfg = CFG::buildCFG(fn, fn->getBody(), result.Context, opts);
  if (!cfg)
    return;
  std::vector<const CFGBlock *> blocks;
  for (const CFGBlock *b : *cfg)
    blocks.push_back(b);

  // May-analysis: a resource is live at a point if some path reaching it has not consumed it.
  std::vector<Live> out(cfg->getNumBlockIDs());
  for (bool changed = true; changed;) {
    changed = false;
    for (const CFGBlock *b : blocks) {
      Live s = b == &cfg->getEntry() ? entry : Live{};
      for (const CFGBlock::AdjacentBlock &pred : b->preds())
        if (pred.isReachable())
          for (const auto &kv : out[pred->getBlockID()])
            s.emplace(kv);
      for (const CFGElement &el : *b)
        if (auto cs = el.getAs<CFGStmt>())
          step(cs->getStmt(), s);
      if (s != out[b->getBlockID()]) {
        out[b->getBlockID()] = std::move(s);
        changed = true;
      }
    }
  }

  std::set<const VarDecl *> reported;
  for (const CFGBlock::AdjacentBlock &pred : cfg->getExit().preds()) {
    if (!pred.isReachable() || pred->hasNoReturnElement())
      continue;
    SourceLocation loc = fn->getBody()->getEndLoc();
    for (const CFGElement &el : *pred.getReachableBlock())
      if (auto cs = el.getAs<CFGStmt>(); cs && llvm::isa<ReturnStmt>(cs->getStmt()))
        loc = cs->getStmt()->getBeginLoc();
    for (const auto &[vd, declLoc] : out[pred->getBlockID()]) {
      if (!reported.insert(vd).second || exempt(*result.SourceManager, declLoc))
        continue;
      diag(loc, "resource %0 of must-consume type %1 is dropped without being consumed on this path")
          << vd << vd->getType().getNonReferenceType();
      diag(declLoc, "declared here", DiagnosticIDs::Note);
      howToFix(loc, "consume it on every path: call its RELOCO_CONSUMES member ('x.commit();'), hand it over "
                    "('sink(std::move(x));'), or return it; if dropping is intentional, suppress with NOLINT and a reason");
    }
  }
}

} // namespace reloco::tidy
