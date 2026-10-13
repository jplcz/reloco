// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/Analysis/CFG.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <set>
#include <vector>

namespace reloco::tidy {

namespace {

// A moved place: a local/parameter (or `this`, with a null root) plus a field path. `x` moved whole has an
// empty path; `std::move(x.f.g)` moves the place x -> f -> g and leaves x.h valid.
struct MovedKey {
  const VarDecl *root = nullptr;
  std::vector<const FieldDecl *> path;
  bool operator==(const MovedKey &o) const { return root == o.root && path == o.path; }
  bool operator<(const MovedKey &o) const { return std::tie(root, path) < std::tie(o.root, o.path); }
  // One path is a prefix of the other: the two places share storage.
  [[nodiscard]] bool overlaps(const MovedKey &o) const {
    if (root != o.root)
      return false;
    const size_t n = std::min(path.size(), o.path.size());
    return std::equal(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(n), o.path.begin());
  }
  [[nodiscard]] bool isThis() const { return root == nullptr; }
  [[nodiscard]] std::string str() const {
    std::string s = root ? root->getName().str() : "this";
    for (const FieldDecl *f : path)
      s += (root ? "." : "->") + f->getName().str();
    return s;
  }
};

using MovedSet = std::map<MovedKey, SourceLocation>;

bool isStdMove(const CallExpr *call) {
  const FunctionDecl *fd = call->getDirectCallee();
  if (!fd || !fd->getIdentifier() || fd->getName() != "move" || call->getNumArgs() != 1)
    return false;
  const auto *ns = llvm::dyn_cast<NamespaceDecl>(fd->getDeclContext()->getRedeclContext());
  return ns && ns->getIdentifier() && ns->getName() == "std";
}

// Resolves `x`, `x.f.g`, `this->f` (or an implicit-this member) to a place.
std::optional<MovedKey> placeOfExpr(const Expr *e) {
  std::vector<const FieldDecl *> rev;
  for (e = e ? e->IgnoreParenImpCasts() : nullptr; e;) {
    if (const auto *me = llvm::dyn_cast<MemberExpr>(e)) {
      const auto *fd = llvm::dyn_cast<FieldDecl>(me->getMemberDecl());
      if (!fd)
        return std::nullopt;
      const Expr *base = me->getBase()->IgnoreParenImpCasts();
      if (me->isArrow() && !llvm::isa<CXXThisExpr>(base))
        return std::nullopt;
      rev.push_back(fd);
      e = base;
      continue;
    }
    MovedKey key;
    if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e)) {
      const auto *vd = llvm::dyn_cast<VarDecl>(dre->getDecl());
      if (!vd || vd->isStaticLocal() || vd->hasGlobalStorage())
        return std::nullopt;
      key.root = vd;
    } else if (!llvm::isa<CXXThisExpr>(e)) {
      return std::nullopt;
    }
    key.path.assign(rev.rbegin(), rev.rend());
    return key;
  }
  return std::nullopt;
}

// The place moved by `std::move(place)`, or nullopt when the move cannot leave it in a moved-from state.
std::optional<MovedKey> movedPlace(const CallExpr *call) {
  const Expr *arg = call->getArg(0);
  const QualType t = arg->getType().getNonReferenceType();
  if (t.isNull() || t->isDependentType() || t->isScalarType() || t.isTriviallyCopyableType(call->getCalleeDecl()->getASTContext()))
    return std::nullopt;
  auto key = placeOfExpr(arg);
  if (key && key->isThis() && key->path.empty())
    return std::nullopt; // std::move(*this) is not a field move
  return key;
}

bool isReinitName(llvm::StringRef n) {
  return n == "clear" || n == "reset" || n == "assign" || n == "emplace" || n == "emplace_back" || n == "reinit" ||
         n == "init";
}

class Analysis {
public:
  Analysis(ASTContext &ctx) : ctx_(ctx) {}

  // The place expression a root (variable reference or `this`) is the start of, e.g. `x` in `x.f.g`.
  const Expr *climb(const Expr *root) {
    const Expr *top = root;
    for (;;) {
      const Stmt *p = parentOf(top);
      const auto *me = p ? llvm::dyn_cast<MemberExpr>(p) : nullptr;
      if (!me || !llvm::isa<FieldDecl>(me->getMemberDecl()) || me->getBase()->IgnoreParenImpCasts() != top->IgnoreParenImpCasts() ||
          (me->isArrow() && !llvm::isa<CXXThisExpr>(root)))
        return top;
      top = me;
    }
  }

  // `x = ...`, `x.f = ...` (builtin or overloaded): the place is valid again afterwards.
  static std::optional<MovedKey> assignedPlace(const Stmt *s) {
    const Expr *lhs = nullptr;
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(s); bo && bo->isAssignmentOp())
      lhs = bo->getLHS();
    else if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(s); oc && oc->isAssignmentOp() && oc->getNumArgs())
      lhs = oc->getArg(0);
    return lhs ? placeOfExpr(lhs) : std::nullopt;
  }

  // `x.clear()` and friends re-establish a known state.
  static std::optional<MovedKey> reinitPlace(const Stmt *s) {
    const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(s);
    if (!mc || !mc->getMethodDecl() || !mc->getMethodDecl()->getIdentifier() ||
        !isReinitName(mc->getMethodDecl()->getName()))
      return std::nullopt;
    return placeOfExpr(mc->getImplicitObjectArgument());
  }

  // The place is only a target being (re)initialised, not a use of the old value.
  bool isReinitTarget(const Expr *top) {
    const Stmt *p = parentOf(top);
    if (!p)
      return false;
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(p); bo && bo->isAssignmentOp() && bo->getLHS()->IgnoreParenImpCasts() == top)
      return true;
    if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(p);
        oc && oc->isAssignmentOp() && oc->getNumArgs() && oc->getArg(0)->IgnoreParenImpCasts() == top)
      return true;
    if (const auto *me = llvm::dyn_cast<MemberExpr>(p))
      if (const Stmt *pp = parentOf(me))
        if (const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(pp); mc && mc->getMethodDecl() &&
                                                                   mc->getMethodDecl()->getIdentifier() &&
                                                                   isReinitName(mc->getMethodDecl()->getName()))
          return true;
    return false;
  }

  // Applies one CFG element; reports uses of moved places when `report` is set.
  template <typename Report> void step(const Stmt *s, MovedSet &moved, Report &&report) {
    const Expr *root = nullptr;
    if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(s); dre && llvm::isa<VarDecl>(dre->getDecl()))
      root = dre;
    else if (llvm::isa<CXXThisExpr>(s))
      root = llvm::cast<CXXThisExpr>(s);
    if (root) {
      if (moved.empty())
        return;
      const Expr *top = climb(root);
      auto key = placeOfExpr(top);
      // A bare `this` (method call, `*this`) is not a use of one moved member: that would flag every idiom.
      if (!key || (key->isThis() && key->path.empty()) || isReinitTarget(top))
        return;
      for (const auto &[moved_key, at] : moved)
        if (moved_key.overlaps(*key)) {
          report(root, *key, at);
          return;
        }
      return;
    }
    if (const auto *call = llvm::dyn_cast<CallExpr>(s); call && isStdMove(call)) {
      if (auto key = movedPlace(call))
        moved[*key] = call->getBeginLoc();
      return;
    }
    // A declaration creates a new object: whatever an earlier loop iteration moved from is a different variable.
    if (const auto *ds = llvm::dyn_cast<DeclStmt>(s)) {
      for (const Decl *d : ds->decls())
        if (const auto *vd = llvm::dyn_cast<VarDecl>(d))
          std::erase_if(moved, [&](const auto &kv) { return kv.first.root == vd; });
      return;
    }
    std::optional<MovedKey> revived = assignedPlace(s);
    if (!revived)
      revived = reinitPlace(s);
    if (revived)
      std::erase_if(moved, [&](const auto &kv) {
        const MovedKey &k = kv.first;
        return k.root == revived->root && k.path.size() >= revived->path.size() &&
               std::equal(revived->path.begin(), revived->path.end(), k.path.begin());
      });
  }

private:
  const Stmt *parentOf(const Stmt *s) {
    for (const Stmt *cur = s;;) {
      auto parents = ctx_.getParents(*cur);
      if (parents.empty())
        return nullptr;
      const Stmt *p = parents[0].get<Stmt>();
      if (!p)
        return nullptr;
      if (llvm::isa<ImplicitCastExpr>(p) || llvm::isa<ParenExpr>(p)) {
        cur = p;
        continue;
      }
      return p;
    }
  }

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
          an.step(cs->getStmt(), s, [](const Expr *, const MovedKey &, SourceLocation) {});
      if (s != out[b->getBlockID()]) {
        out[b->getBlockID()] = std::move(s);
        changed = true;
      }
    }
  }

  std::set<const Expr *> reported;
  for (const CFGBlock *b : blocks) {
    MovedSet s = in[b->getBlockID()];
    for (const CFGElement &el : *b)
      if (auto cs = el.getAs<CFGStmt>())
        an.step(cs->getStmt(), s, [&](const Expr *use, const MovedKey &key, SourceLocation movedAt) {
          if (!reported.insert(use).second || exempt(*result.SourceManager, use->getBeginLoc()))
            return;
          diag(use->getBeginLoc(), "%0 is used after it was moved from") << key.str();
          diag(movedAt, "moved here", DiagnosticIDs::Note);
          howToFix(use->getBeginLoc(), "reassign it ('x = T{};') or call a reinitialising member ('x.clear();') before "
                                       "reuse, copy explicitly ('T copy(x);') instead of moving, or restructure so the "
                                       "moved-from value is never touched again (Rust rejects this at compile time)");
        });
  }
}

} // namespace reloco::tidy
