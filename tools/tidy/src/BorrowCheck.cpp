// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-borrow-check: best-effort, intra-procedural emulation of Rust's borrow rules.
//
//  * A *borrow* is a local reference / raw pointer / gsl::Pointer view (span, string_view, value_ref...)
//    initialised from a *place*: a local or parameter plus an optional field path.
//  * A borrow is live (non-lexical lifetimes) from its initialiser to its last use; a use inside a loop
//    that does not contain the borrow's declaration extends the borrow to the end of that loop.
//  * While a shared borrow is live, mutating an overlapping place is an error; while a mutable borrow
//    is live, any other use of an overlapping place is an error. Disjoint field paths do not overlap.
//  * `for (auto &e : c)` borrows `c` for the whole loop body, which catches iterator invalidation.
//
// Exemptions: RELOCO_BORROW_EXEMPT on a function or class, RELOCO_BEGIN/END_UNSAFE_CODE,
// RELOCO_BEGIN/END_UNSAFE_BUFFER_USAGE, unsafe_* / RELOCO_UNSAFE_BUFFER_USAGE functions, NOLINT.

#include "Checks.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace reloco::tidy {

namespace {

struct Place {
  const VarDecl *var = nullptr;
  llvm::SmallVector<const FieldDecl *, 2> path;

  explicit operator bool() const { return var != nullptr; }

  // One path is a prefix of the other: the places share storage.
  [[nodiscard]] bool overlaps(const Place &o) const {
    if (var != o.var)
      return false;
    const size_t n = std::min(path.size(), o.path.size());
    return std::equal(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(n), o.path.begin());
  }
};

const Expr *strip(const Expr *e) {
  while (e) {
    const Expr *next = e->IgnoreParenImpCasts();
    if (const auto *m = llvm::dyn_cast<MaterializeTemporaryExpr>(next))
      next = m->getSubExpr();
    else if (const auto *b = llvm::dyn_cast<CXXBindTemporaryExpr>(next))
      next = b->getSubExpr();
    else if (const auto *c = llvm::dyn_cast<ExprWithCleanups>(next))
      next = c->getSubExpr();
    else if (const auto *f = llvm::dyn_cast<CXXFunctionalCastExpr>(next))
      next = f->getSubExpr();
    if (next == e)
      return e;
    e = next;
  }
  return e;
}

bool isAliasingType(QualType t) {
  if (t.isNull() || t->isDependentType())
    return false;
  if (t->isReferenceType() || t->isPointerType())
    return true;
  const CXXRecordDecl *rd = t.getCanonicalType()->getAsCXXRecordDecl();
  return rd && rd->hasDefinition() && rd->hasAttr<PointerAttr>();
}

bool isTrackedVar(const VarDecl *v) {
  return v && v->hasLocalStorage() && !v->isImplicit();
}

// Resolves the storage an expression designates or aliases; empty when it is not rooted in a
// tracked local/parameter (temporaries, globals, heap, unknown calls).
QualType calleeReturn(const CallExpr *c) {
  const FunctionDecl *fd = c->getDirectCallee();
  return fd ? fd->getReturnType() : c->getType();
}

Place placeOf(const Expr *e) {
  e = strip(e);
  if (!e)
    return {};
  if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e)) {
    const auto *v = llvm::dyn_cast<VarDecl>(dre->getDecl());
    return isTrackedVar(v) ? Place{v, {}} : Place{};
  }
  if (const auto *me = llvm::dyn_cast<MemberExpr>(e)) {
    if (me->isArrow())
      return {};
    Place p = placeOf(me->getBase());
    if (!p)
      return {};
    if (const auto *fd = llvm::dyn_cast<FieldDecl>(me->getMemberDecl()))
      p.path.push_back(fd);
    return p;
  }
  if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
    if (uo->getOpcode() == UO_AddrOf || uo->getOpcode() == UO_Deref)
      return placeOf(uo->getSubExpr());
    return {};
  }
  if (const auto *as = llvm::dyn_cast<ArraySubscriptExpr>(e))
    return placeOf(as->getBase());
  if (const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(e)) {
    if (const Expr *obj = mc->getImplicitObjectArgument())
      return isAliasingType(calleeReturn(mc)) ? placeOf(obj) : Place{};
    return {};
  }
  if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(e)) {
    if (oc->getNumArgs() > 0 && isAliasingType(calleeReturn(oc)))
      return placeOf(oc->getArg(0));
    return {};
  }
  if (const auto *ce = llvm::dyn_cast<CXXConstructExpr>(e)) {
    // Views are built from the storage they borrow: span(buf), string_view(s), value_ref(x).
    if (ce->getNumArgs() > 0 && isAliasingType(ce->getType()))
      return placeOf(ce->getArg(0));
    return {};
  }
  if (const auto *call = llvm::dyn_cast<CallExpr>(e)) {
    const FunctionDecl *fd = call->getDirectCallee();
    if (!fd || !isAliasingType(calleeReturn(call)))
      return {};
    for (unsigned i = 0; i < call->getNumArgs() && i < fd->getNumParams(); ++i)
      if (fd->getParamDecl(i)->hasAttr<LifetimeBoundAttr>())
        if (Place p = placeOf(call->getArg(i)))
          return p;
    return {};
  }
  return {};
}

bool isMutableTarget(QualType t) {
  t = t.getNonReferenceType();
  if (t->isPointerType())
    return !t->getPointeeType().isConstQualified();
  if (const auto *rd = t->getAsCXXRecordDecl()) {
    // gsl::Pointer views are templated on the element: span<T> mutable, span<const T> shared.
    if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd)) {
      const auto &args = spec->getTemplateArgs();
      if (args.size() > 0 && args[0].getKind() == TemplateArgument::Type)
        return !args[0].getAsType().isConstQualified();
    }
    return false;
  }
  return !t.isConstQualified();
}

struct Borrow {
  const VarDecl *var = nullptr;     // the borrowing variable; null for a range-for
  Place place;
  bool mut = false;
  SourceLocation declLoc;           // diagnostic anchor for the note
  SourceLocation start;             // end of the initialiser
  SourceLocation end;               // last use (loop-extended)
  SourceLocation lastUse;
};

enum class Access { read, mutate, borrow_mut };

class Analyzer : public RecursiveASTVisitor<Analyzer> {
public:
  using Reporter = std::function<void(const DeclRefExpr *, const Borrow &, Access)>;

  Analyzer(ASTContext &ctx) : ctx_(ctx), sm_(ctx.getSourceManager()) {}

  bool shouldVisitTemplateInstantiations() const { return false; }

  bool VisitVarDecl(VarDecl *vd) {
    if (!isTrackedVar(vd) || llvm::isa<ParmVarDecl>(vd) || !vd->hasInit())
      return true;
    const QualType t = vd->getType();
    if (t.isNull() || t->isDependentType() || !isAliasingType(t))
      return true;
    Place p = placeOf(vd->getInit());
    if (!p || p.var == vd)
      return true;
    Borrow b;
    b.var = vd;
    b.place = std::move(p);
    b.mut = isMutableTarget(t) && !b.place.var->getType().getNonReferenceType().isConstQualified();
    b.declLoc = vd->getLocation();
    b.start = vd->getInit()->getEndLoc();
    borrows_.push_back(std::move(b));
    return true;
  }

  bool VisitCXXForRangeStmt(CXXForRangeStmt *fs) {
    const Expr *init = fs->getRangeInit();
    Place p = placeOf(init);
    if (!p || !fs->getLoopVariable())
      return true;
    Borrow b;
    b.place = std::move(p);
    const QualType lt = fs->getLoopVariable()->getType();
    b.mut = lt->isReferenceType() && isMutableTarget(lt) &&
            !b.place.var->getType().getNonReferenceType().isConstQualified();
    b.declLoc = fs->getForLoc();
    b.start = fs->getBody()->getBeginLoc();
    b.end = fs->getBody()->getEndLoc();
    b.lastUse = b.end;
    borrows_.push_back(std::move(b));
    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *dre) {
    if (const auto *v = llvm::dyn_cast<VarDecl>(dre->getDecl()); isTrackedVar(v))
      refs_.push_back(dre);
    return true;
  }

  void finish(const Reporter &report) {
    computeLiveRanges();
    for (const Borrow &b : borrows_)
      for (const DeclRefExpr *dre : refs_) {
        const auto *v = llvm::cast<VarDecl>(dre->getDecl());
        if (v != b.place.var && v != b.var)
          continue;
        if (v == b.var || !later(dre->getBeginLoc(), b.start) || later(dre->getBeginLoc(), b.end))
          continue;
        Place q;
        const Expr *top = topOfPlace(dre, q);
        if (!q || !q.overlaps(b.place))
          continue;
        const Access a = classify(top);
        if (!b.mut && a == Access::read)
          continue;
        report(dre, b, a);
      }
  }


private:
  bool later(SourceLocation a, SourceLocation b) const { return sm_.isBeforeInTranslationUnit(b, a); }

  // Climbs from a DeclRefExpr through field accesses to the full place expression.
  const Expr *topOfPlace(const DeclRefExpr *dre, Place &out) {
    const Expr *top = dre;
    out = Place{llvm::cast<VarDecl>(dre->getDecl()), {}};
    for (;;) {
      const auto *parent = parentOf(top);
      const auto *me = parent ? llvm::dyn_cast<MemberExpr>(parent) : nullptr;
      if (!me || me->isArrow() || strip(me->getBase()) != strip(top))
        break;
      if (const auto *fd = llvm::dyn_cast<FieldDecl>(me->getMemberDecl()))
        out.path.push_back(fd);
      else
        break;
      top = me;
    }
    return top;
  }

  const Stmt *parentOf(const Stmt *s) {
    for (const Stmt *cur = s;;) {
      auto parents = ctx_.getParents(*cur);
      if (parents.empty())
        return nullptr;
      const Stmt *p = parents[0].get<Stmt>();
      if (!p)
        return nullptr;
      if (llvm::isa<ImplicitCastExpr>(p) || llvm::isa<ParenExpr>(p) || llvm::isa<MaterializeTemporaryExpr>(p) ||
          llvm::isa<CXXBindTemporaryExpr>(p) || llvm::isa<ExprWithCleanups>(p)) {
        cur = p;
        continue;
      }
      return p;
    }
  }

  // The variable whose initialiser is exactly this expression (binding a new reference/view).
  const VarDecl *initializedVar(const Expr *e) {
    for (DynTypedNode cur = DynTypedNode::create(*e);;) {
      auto parents = ctx_.getParents(cur);
      if (parents.empty())
        return nullptr;
      cur = parents[0];
      if (const auto *vd = cur.get<VarDecl>())
        return vd;
      const auto *st = cur.get<Stmt>();
      if (!st || !(llvm::isa<ImplicitCastExpr>(st) || llvm::isa<ParenExpr>(st) ||
                   llvm::isa<MaterializeTemporaryExpr>(st) || llvm::isa<CXXBindTemporaryExpr>(st) ||
                   llvm::isa<ExprWithCleanups>(st) || llvm::isa<CXXConstructExpr>(st)))
        return nullptr;
    }
  }

  // How the place expression is used by its parent.
  Access classify(const Expr *top) {
    if (const VarDecl *init = initializedVar(top))
      return isMutableTarget(init->getType()) && isAliasingType(init->getType()) ? Access::borrow_mut : Access::read;
    const Stmt *parent = parentOf(top);
    if (!parent)
      return Access::read;
    if (const auto *me = llvm::dyn_cast<MemberExpr>(parent))
      if (const auto *md = llvm::dyn_cast<CXXMethodDecl>(me->getMemberDecl()); md && strip(me->getBase()) == strip(top))
        return md->isConst() ? Access::read : Access::mutate;
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(parent))
      return bo->isAssignmentOp() && strip(bo->getLHS()) == strip(top) ? Access::mutate : Access::read;
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(parent)) {
      if (uo->isIncrementDecrementOp())
        return Access::mutate;
      if (uo->getOpcode() == UO_AddrOf)
        return top->getType().isConstQualified() ? Access::read : Access::borrow_mut;
      return Access::read;
    }
    if (const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(parent)) {
      if (strip(mc->getImplicitObjectArgument()) == strip(top))
        if (const auto *md = mc->getMethodDecl())
          return md->isConst() ? Access::read : Access::mutate;
      return argAccess(mc->getDirectCallee(), mc, top);
    }
    if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(parent)) {
      if (const auto *md = llvm::dyn_cast_or_null<CXXMethodDecl>(oc->getDirectCallee());
          md && oc->getNumArgs() > 0 && strip(oc->getArg(0)) == strip(top))
        return md->isConst() ? Access::read : Access::mutate;
      return argAccess(oc->getDirectCallee(), oc, top);
    }
    if (const auto *call = llvm::dyn_cast<CallExpr>(parent))
      return argAccess(call->getDirectCallee(), call, top);
    if (const auto *ce = llvm::dyn_cast<CXXConstructExpr>(parent))
      return argAccess(ce->getConstructor(), ce, top);
    return Access::read;
  }

  template <typename CallLike> static Access argAccess(const FunctionDecl *fd, const CallLike *call, const Expr *top) {
    if (!fd)
      return Access::read;
    const unsigned skip = llvm::isa<CXXOperatorCallExpr>(call) && llvm::isa<CXXMethodDecl>(fd) ? 1u : 0u;
    for (unsigned i = 0; i < call->getNumArgs(); ++i) {
      if (strip(call->getArg(i)) != strip(top) || i < skip)
        continue;
      const unsigned pi = i - skip;
      if (pi >= fd->getNumParams())
        return Access::read;
      const QualType pt = fd->getParamDecl(pi)->getType();
      if (pt->isRValueReferenceType())
        return Access::mutate; // moved from
      if (pt->isLValueReferenceType() && !pt.getNonReferenceType().isConstQualified())
        return Access::borrow_mut;
    }
    return Access::read;
  }

  void computeLiveRanges() {
    for (Borrow &b : borrows_) {
      if (!b.var)
        continue; // range-for: whole loop body
      b.end = b.start;
      b.lastUse = b.start;
      for (const DeclRefExpr *dre : refs_) {
        if (dre->getDecl() != b.var)
          continue;
        SourceLocation use = dre->getEndLoc();
        // A use inside a loop that does not contain the declaration is live until the loop ends.
        for (const Stmt *cur = dre;;) {
          auto parents = ctx_.getParents(*cur);
          if (parents.empty() || !parents[0].get<Stmt>())
            break;
          cur = parents[0].get<Stmt>();
          if (llvm::isa<ForStmt>(cur) || llvm::isa<WhileStmt>(cur) || llvm::isa<DoStmt>(cur) ||
              llvm::isa<CXXForRangeStmt>(cur)) {
            const SourceRange r = cur->getSourceRange();
            const bool containsDecl = !later(b.declLoc, r.getEnd()) && !later(r.getBegin(), b.declLoc);
            if (!containsDecl && later(r.getEnd(), use))
              use = r.getEnd();
          }
        }
        if (later(use, b.end)) {
          b.end = use;
          b.lastUse = dre->getBeginLoc();
        }
      }
    }
  }

  ASTContext &ctx_;
  const SourceManager &sm_;

public:
  std::vector<Borrow> borrows_;
  std::vector<const DeclRefExpr *> refs_;
};

bool hasExemptAnnotation(const Decl *d) {
  for (; d; d = Decl::castFromDeclContext(d->getDeclContext())) {
    for (const auto *an : d->specific_attrs<AnnotateAttr>())
      if (an->getAnnotation() == "reloco_borrow_exempt")
        return true;
    if (llvm::isa<TranslationUnitDecl>(d))
      break;
  }
  return false;
}

} // namespace

void BorrowCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit()), unless(isTemplateInstantiation())).bind("fn"),
                     this);
}

void BorrowCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->hasBody() || fn->isTemplateInstantiation() || hasExemptAnnotation(fn) || inUnsafeTier(fn) ||
      exempt(*result.SourceManager, fn->getLocation()))
    return;
  if (const auto *md = llvm::dyn_cast<CXXMethodDecl>(fn); md && md->getParent()->isLambda())
    return; // analysed as part of the enclosing function

  Analyzer an(*result.Context);
  an.TraverseStmt(fn->getBody());

  const SourceManager &sm = *result.SourceManager;
  an.finish([&](const DeclRefExpr *use, const Borrow &b, Access a) {
    if (exempt(sm, use->getBeginLoc()) || exempt(sm, b.declLoc))
      return;
    const llvm::StringRef what = a == Access::read ? "use" : a == Access::mutate ? "mutation" : "mutable borrow";
    const std::string borrower = b.var ? "'" + b.var->getName().str() + "'" : "the enclosing range-for loop";
    diag(use->getBeginLoc(), "%0 of '%1' while it is %2 borrowed by %3")
        << what << use->getDecl()->getName() << (b.mut ? "mutably" : "shared") << borrower;
    diag(b.declLoc, "borrow created here", DiagnosticIDs::Note);
    if (b.var)
      diag(b.lastUse, "borrow is still used here", DiagnosticIDs::Note);
    howToFix(use->getBeginLoc(), "end the borrow first (copy the value out: 'int v = r;' and use v later), reorder so the "
                                 "last use of the borrow comes before this access, or borrow disjoint fields. If the "
                                 "aliasing is sound, mark the function RELOCO_BORROW_EXEMPT with a comment");
  });
}

} // namespace reloco::tidy
