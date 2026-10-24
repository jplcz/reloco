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

#include "LifetimeNames.h"
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

// The root of a place is a tracked local/parameter, or the enclosing method standing for `this`.
struct Place {
  const ValueDecl *var = nullptr;
  llvm::SmallVector<const FieldDecl *, 2> path;
  // The place is the object a pointer at `var.path` points to (reached through `*p` / `p->`), not `var.path` itself.
  bool pointee = false;

  explicit operator bool() const { return var != nullptr; }

  // One path is a prefix of the other: the places share storage. A pointee is another object than the pointer's own
  // storage, so it only compares with a pointee of the same pointer.
  [[nodiscard]] bool overlaps(const Place &o) const {
    if (var != o.var || pointee != o.pointee)
      return false;
    if (pointee)
      return path == o.path;
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

// A class with a RELOCO_LT(name) field holds a borrow: it behaves like a view for the borrow checker.
bool isLabelledHandle(const CXXRecordDecl *rd) {
  if (!rd || !rd->hasDefinition())
    return false;
  for (const FieldDecl *f : rd->fields())
    for (const std::string &n : declLifetimes(f))
      if (n != "static")
        return true;
  return false;
}

// Whether some field of `rd` carries one of the labels in `names` (the labelled result is then read out of the
// object's stored borrow rather than out of the object itself).
bool fieldCarries(const CXXRecordDecl *rd, const LifetimeSet &names) {
  if (!rd || !rd->hasDefinition())
    return false;
  for (const FieldDecl *f : rd->fields())
    if (sharesLifetime(declLifetimes(f), names))
      return true;
  return false;
}

bool isAliasingType(QualType t) {
  if (t.isNull() || t->isDependentType())
    return false;
  if (t->isReferenceType() || t->isPointerType())
    return true;
  const CXXRecordDecl *rd = t.getCanonicalType()->getAsCXXRecordDecl();
  return rd && rd->hasDefinition() && (rd->hasAttr<PointerAttr>() || isLabelledHandle(rd));
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

// The method whose `this` roots places in the function being analysed (null outside methods).
const ValueDecl *g_thisRoot = nullptr;

bool isThisRoot(const ValueDecl *v) { return llvm::isa_and_nonnull<FunctionDecl>(v); }

bool rootIsConst(const ValueDecl *v) {
  if (const auto *md = llvm::dyn_cast_or_null<CXXMethodDecl>(v))
    return md->isConst();
  return v && v->getType().getNonReferenceType().isConstQualified();
}

// Whether a pointer/view returned by `fd` keeps borrowing the receiver. A library method that returns a bare
// `T *` without RELOCO_LIFETIMEBOUND hands out a handle to an object the receiver merely links to
// (intrusive-list `pop_front()`, lookup tables), not to the receiver's own storage. Standard-library and
// non-pointer results keep the conservative answer.
bool receiverBorrowed(const FunctionDecl *fd) {
  if (!fd || !fd->getReturnType()->isPointerType() || fd->hasAttr<LifetimeBoundAttr>())
    return true;
  const auto *md = llvm::dyn_cast<CXXMethodDecl>(fd);
  if (md) {
    for (QualType t = md->getType();;) {
      if (const auto *at = llvm::dyn_cast<AttributedType>(t.getTypePtr()); at && at->getAttrKind() == attr::LifetimeBound)
        return true;
      const QualType next = t.getSingleStepDesugaredType(md->getASTContext());
      if (next == t)
        break;
      t = next;
    }
  }
  const DeclContext *dc = fd->getDeclContext()->getEnclosingNamespaceContext();
  while (dc && dc->getParent() && !dc->getParent()->isTranslationUnit())
    dc = dc->getParent()->getEnclosingNamespaceContext();
  const auto *ns = llvm::dyn_cast_or_null<NamespaceDecl>(dc);
  return ns && (ns->getName() == "std" || ns->getName() == "__gnu_cxx" || ns->getName() == "__cxx11");
}

Place placeOf(const Expr *e);

// A pointer value read out of a place (`string_view(argv[0])`, `span(p.data_)`) designates what that pointer
// points at, not the place that stores the pointer.
Place placeOfPointerRead(const Expr *e) {
  for (const Expr *cur = e->IgnoreParens(); const auto *ic = llvm::dyn_cast<ImplicitCastExpr>(cur);
       cur = ic->getSubExpr()->IgnoreParens()) {
    if (ic->getCastKind() == CK_LValueToRValue && ic->getType()->isPointerType()) {
      Place p = placeOf(ic->getSubExpr());
      if (p)
        p.pointee = !p.pointee;
      return p;
    }
  }
  return placeOf(e);
}

bool isMutableTarget(QualType t);

// The places a labelled handle constructor call borrows: one per argument bound to a RELOCO_LT(name) parameter.
// `mut` tells, per place, whether the argument hands out mutable access.
std::vector<Place> ctorLabelPlaces(const CXXConstructExpr *ce, std::vector<bool> *mut = nullptr) {
  std::vector<Place> out;
  const CXXConstructorDecl *ctor = ce->getConstructor();
  if (!ctor || ctor->isCopyOrMoveConstructor())
    return out;
  for (unsigned i = 0; i < ce->getNumArgs() && i < ctor->getNumParams(); ++i) {
    bool labelled = false;
    for (const std::string &n : declParamLifetimes(ctor, i))
      labelled = labelled || n != "static";
    if (!labelled)
      continue;
    Place p = placeOfPointerRead(ce->getArg(i));
    if (!p)
      continue;
    out.push_back(std::move(p));
    if (mut)
      mut->push_back(isMutableTarget(ce->getArg(i)->getType()));
  }
  return out;
}

Place placeOf(const Expr *e) {
  e = strip(e);
  if (!e)
    return {};
  if (llvm::isa<CXXThisExpr>(e))
    return g_thisRoot ? Place{g_thisRoot, {}} : Place{};
  if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e)) {
    const auto *v = llvm::dyn_cast<VarDecl>(dre->getDecl());
    return isTrackedVar(v) ? Place{v, {}} : Place{};
  }
  if (const auto *me = llvm::dyn_cast<MemberExpr>(e)) {
    if (me->isArrow() && !llvm::isa<CXXThisExpr>(strip(me->getBase())))
      return {};
    Place p = placeOf(me->getBase());
    if (!p)
      return {};
    if (const auto *fd = llvm::dyn_cast<FieldDecl>(me->getMemberDecl()))
      p.path.push_back(fd);
    return p;
  }
  if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
    if (uo->getOpcode() == UO_AddrOf)
      return placeOf(uo->getSubExpr());
    if (uo->getOpcode() == UO_Deref) {
      Place p = placeOf(uo->getSubExpr());
      // `*this` is the object itself; `*p` for any other pointer is the pointee.
      if (p && !llvm::isa<CXXThisExpr>(strip(uo->getSubExpr())))
        p.pointee = !p.pointee;
      return p;
    }
    return {};
  }
  if (const auto *as = llvm::dyn_cast<ArraySubscriptExpr>(e))
    return placeOf(as->getBase());
  if (const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(e)) {
    if (const Expr *obj = mc->getImplicitObjectArgument()) {
      // RELOCO_LT_RETURN states exactly which arguments and which receiver the result borrows.
      if (const FunctionDecl *fd = mc->getDirectCallee()) {
        if (const LifetimeSet ret = declReturnLifetimes(fd); !ret.empty()) {
          for (unsigned i = 0; i < mc->getNumArgs(); ++i)
            if (paramFlowsToReturn(fd, i))
              if (Place p = placeOf(mc->getArg(i)))
                return p;
          if (!sharesLifetime(declThisLifetimes(fd), ret))
            return {};
        }
      }
      if (!isAliasingType(calleeReturn(mc)) || !receiverBorrowed(mc->getDirectCallee()))
        return {};
      Place p = placeOf(obj);
      // `p->m()`: the receiver is the pointee, not the pointer's own storage.
      const auto *callee = llvm::dyn_cast<MemberExpr>(mc->getCallee());
      if (p && callee && callee->isArrow() && !llvm::isa<CXXThisExpr>(strip(obj)))
        p.pointee = !p.pointee;
      return p;
    }
    return {};
  }
  if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(e)) {
    if (oc->getNumArgs() > 0 && isAliasingType(calleeReturn(oc)) && receiverBorrowed(oc->getDirectCallee())) {
      Place p = placeOf(oc->getArg(0));
      // `view->m` / `*view` reach the object the view points at, not the view's own storage.
      const bool deref = oc->getOperator() == OO_Arrow || (oc->getOperator() == OO_Star && oc->getNumArgs() == 1);
      if (p && deref && isAliasingType(oc->getArg(0)->getType()))
        p.pointee = !p.pointee;
      return p;
    }
    return {};
  }
  if (const auto *ce = llvm::dyn_cast<CXXConstructExpr>(e)) {
    if (isLabelledHandle(ce->getType().getCanonicalType()->getAsCXXRecordDecl())) {
      const std::vector<Place> labelled = ctorLabelPlaces(ce);
      return labelled.empty() ? Place{} : labelled.front();
    }
    // Views are built from the storage they borrow: span(buf), string_view(s), value_ref(x).
    if (ce->getNumArgs() > 0 && isAliasingType(ce->getType()) && !ce->getConstructor()->isCopyOrMoveConstructor())
      return placeOfPointerRead(ce->getArg(0));
    return {};
  }
  if (const auto *call = llvm::dyn_cast<CallExpr>(e)) {
    const FunctionDecl *fd = call->getDirectCallee();
    if (!fd || !isAliasingType(calleeReturn(call)))
      return {};
    if (hasStatic(declReturnLifetimes(fd)))
      return {}; // a 'static result borrows from no caller-owned object
    for (unsigned i = 0; i < call->getNumArgs() && i < fd->getNumParams(); ++i)
      if (paramFlowsToReturn(fd, i))
        if (Place p = placeOf(call->getArg(i)))
          return p;
    return {};
  }
  return {};
}

// Every argument place a RELOCO_LT_RETURN call result borrows (placeOf only reports the first one).
std::vector<Place> extraBorrowPlaces(const Expr *e) {
  std::vector<Place> out;
  const auto *call = llvm::dyn_cast_or_null<CallExpr>(strip(e));
  const FunctionDecl *fd = call ? call->getDirectCallee() : nullptr;
  if (!fd || llvm::isa<CXXOperatorCallExpr>(call) || declReturnLifetimes(fd).empty() || !isAliasingType(calleeReturn(call)))
    return out;
  for (unsigned i = 0; i < call->getNumArgs() && i < fd->getNumParams(); ++i)
    if (paramFlowsToReturn(fd, i))
      if (Place p = placeOf(call->getArg(i)))
        out.push_back(std::move(p));
  return out;
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
  using Reporter = std::function<void(const Expr *, llvm::StringRef, const Borrow &, Access)>;

  Analyzer(ASTContext &ctx) : ctx_(ctx), sm_(ctx.getSourceManager()) {}

  bool shouldVisitTemplateInstantiations() const { return false; }

  bool VisitVarDecl(VarDecl *vd) {
    if (!isTrackedVar(vd) || llvm::isa<ParmVarDecl>(vd) || !vd->hasInit())
      return true;
    const QualType t = vd->getType();
    if (t.isNull() || t->isDependentType() || !isAliasingType(t))
      return true;
    const Expr *init = strip(vd->getInit());
    // Copying a pointer or view out of another variable copies its value: the copy keeps whatever the original
    // borrows, but does not borrow the original itself.
    if (const auto *ic = llvm::dyn_cast<ImplicitCastExpr>(vd->getInit()->IgnoreParens());
        ic && ic->getCastKind() == CK_LValueToRValue) {
      if (Place src = placeOf(ic->getSubExpr()); src && src.path.empty())
        inheritBorrows(vd, src.var);
      return true;
    }
    if (const auto *ce = llvm::dyn_cast_or_null<CXXConstructExpr>(init);
        ce && ce->getConstructor()->isCopyOrMoveConstructor() && ce->getNumArgs() == 1) {
      if (Place src = placeOf(ce->getArg(0)); src && src.path.empty())
        inheritBorrows(vd, src.var);
      return true;
    }
    // `h.get()` where get() returns a label stored in a field of `h`: the result borrows what `h` borrows, not `h`.
    if (const auto *mc = llvm::dyn_cast_or_null<CXXMemberCallExpr>(init); mc && mc->getMethodDecl() && mc->getImplicitObjectArgument()) {
      const LifetimeSet ret = declReturnLifetimes(mc->getMethodDecl());
      if (!ret.empty() && !hasStatic(ret) && !sharesLifetime(declThisLifetimes(mc->getMethodDecl()), ret) &&
          fieldCarries(mc->getMethodDecl()->getParent(), ret)) {
        if (Place src = placeOf(mc->getImplicitObjectArgument()); src && src.path.empty())
          inheritBorrows(vd, src.var);
        return true;
      }
    }
    std::vector<bool> argMut;
    std::vector<Place> places;
    if (const auto *ce = llvm::dyn_cast_or_null<CXXConstructExpr>(init); ce && isLabelledHandle(t.getCanonicalType()->getAsCXXRecordDecl())) {
      places = ctorLabelPlaces(ce, &argMut);
    } else {
      Place p = placeOf(vd->getInit());
      if (!p || p.var == vd)
        return true;
      // 'auto *self = this;' is a view of the whole object, not a borrow of one member.
      if (isThisRoot(p.var) && p.path.empty() && llvm::isa<CXXThisExpr>(strip(vd->getInit())))
        return true;
      places.push_back(std::move(p));
      for (Place &q : extraBorrowPlaces(vd->getInit()))
        if (std::none_of(places.begin(), places.end(), [&](const Place &o) { return o.var == q.var && o.path == q.path && o.pointee == q.pointee; }))
          places.push_back(std::move(q));
    }
    const bool viaAddress = llvm::isa_and_nonnull<UnaryOperator>(init) && llvm::cast<UnaryOperator>(init)->getOpcode() == UO_AddrOf;
    for (size_t i = 0; i < places.size(); ++i) {
      Place &pl = places[i];
      if (pl.var == vd)
        continue;
      Borrow b;
      b.var = vd;
      b.place = pl;
      b.mut = (argMut.empty() ? isMutableTarget(t) : argMut[i]) && !rootIsConst(b.place.var);
      b.declLoc = vd->getLocation();
      b.start = vd->getInit()->getEndLoc();
      addBorrow(std::move(b));
      // A view derived from another view (`span.data()`, `*ref`) keeps the original storage borrowed as well.
      if (!viaAddress && !isThisRoot(pl.var))
        inheritBorrows(vd, pl.var);
    }
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
    b.mut = lt->isReferenceType() && isMutableTarget(lt) && !rootIsConst(b.place.var);
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

  bool VisitCompoundStmt(CompoundStmt *cs) {
    blocks_.push_back(cs);
    return true;
  }

  bool VisitStmt(Stmt *st) {
    if (llvm::isa<ForStmt>(st) || llvm::isa<WhileStmt>(st) || llvm::isa<DoStmt>(st) || llvm::isa<CXXForRangeStmt>(st))
      loops_.push_back(st);
    return true;
  }

  bool VisitCXXThisExpr(CXXThisExpr *te) {
    if (g_thisRoot)
      refs_.push_back(te);
    return true;
  }

  void finish(const Reporter &report) {
    computeLiveRanges();
    for (const Borrow &b : borrows_)
      for (const Expr *root : refs_) {
        const ValueDecl *v = rootDecl(root);
        if (v != b.place.var && v != b.var)
          continue;
        if (v == b.var || !later(root->getBeginLoc(), b.start) || later(root->getBeginLoc(), b.end))
          continue;
        Place q;
        const Expr *top = topOfPlace(root, q);
        if (!q || !q.overlaps(b.place))
          continue;
        const Access a = classify(top);
        if (!b.mut && a == Access::read)
          continue;
        const std::string name = isThisRoot(q.var) ? (q.path.empty() ? "this" : q.path.front()->getName().str())
                                                    : q.var->getName().str();
        if (leavesAfter(root, b))
          continue;
        report(root, name, b, a);
      }
  }


private:
  void addBorrow(Borrow b) {
    for (const Borrow &o : borrows_)
      if (o.var == b.var && o.place.var == b.place.var && o.place.path == b.place.path && o.place.pointee == b.place.pointee)
        return;
    borrows_.push_back(std::move(b));
  }

  // `vd` is derived from the variable `src`: it keeps everything `src` borrows alive for as long as `vd` is used.
  void inheritBorrows(const VarDecl *vd, const ValueDecl *src) {
    if (!src || src == vd)
      return;
    const QualType t = vd->getType();
    const size_t n = borrows_.size();
    for (size_t i = 0; i < n; ++i) {
      if (borrows_[i].var != src)
        continue;
      Borrow b;
      b.var = vd;
      b.place = borrows_[i].place;
      // A labelled handle holds exactly the access it was built with.
      const bool handle = isLabelledHandle(t.getCanonicalType()->getAsCXXRecordDecl());
      b.mut = (handle || isMutableTarget(t)) && borrows_[i].mut;
      b.declLoc = vd->getLocation();
      b.start = vd->getInit()->getEndLoc();
      addBorrow(std::move(b));
    }
  }

  // A write that is directly followed by `return`/`co_return` (or by `break`/`continue` leaving the loop that
  // the borrow lives in) can never be observed through the borrow, so it is not a conflict.
  bool leavesAfter(const Expr *use, const Borrow &b) {
    // Located by source position: an implicit `this` in a dependent member access has no parent node.
    const SourceLocation at = use->getBeginLoc();
    auto contains = [&](const Stmt *s) {
      return !sm_.isBeforeInTranslationUnit(at, s->getBeginLoc()) && !sm_.isBeforeInTranslationUnit(s->getEndLoc(), at);
    };
    const Stmt *stmt = nullptr;
    const CompoundStmt *block = nullptr;
    const Stmt *loop = nullptr;
    for (const CompoundStmt *cs : blocks_) {
      if (!contains(cs))
        continue;
      for (const Stmt *child : cs->body())
        if (contains(child) && (!block || sm_.isBeforeInTranslationUnit(block->getBeginLoc(), cs->getBeginLoc()))) {
          block = cs;
          stmt = child;
        }
    }
    for (const Stmt *l : loops_)
      if (contains(l) && (!loop || sm_.isBeforeInTranslationUnit(loop->getBeginLoc(), l->getBeginLoc())))
        loop = l;
    if (!block || !stmt)
      return false;
    const ValueDecl *loopVar = nullptr;
    if (const auto *fr = llvm::dyn_cast_or_null<CXXForRangeStmt>(loop))
      loopVar = fr->getLoopVariable();
    auto mentionsBorrow = [&](const Stmt *s) {
      for (const Expr *r : refs_) {
        const ValueDecl *v = rootDecl(r);
        if (v && (v == b.var || v == loopVar) && !sm_.isBeforeInTranslationUnit(r->getBeginLoc(), s->getBeginLoc()) &&
            !sm_.isBeforeInTranslationUnit(s->getEndLoc(), r->getBeginLoc()))
          return true;
      }
      return false;
    };
    auto it = block->body_begin();
    while (it != block->body_end() && *it != stmt)
      ++it;
    if (it == block->body_end())
      return false;
    if (llvm::isa<ReturnStmt>(stmt) || llvm::isa<CoreturnStmt>(stmt))
      return true;
    // Statements between the write and the exit must not read through the borrow.
    for (++it; it != block->body_end(); ++it) {
      const Stmt *next = *it;
      if (llvm::isa<ReturnStmt>(next) || llvm::isa<CoreturnStmt>(next))
        return !mentionsBorrow(next);
      if (llvm::isa<ContinueStmt>(next) || llvm::isa<BreakStmt>(next)) {
        if (!loop)
          return false;
        // The borrow must die with the iteration (declared in the body) or with the loop (the range-for itself).
        const bool declaredInLoop = b.var && !sm_.isBeforeInTranslationUnit(b.var->getLocation(), loop->getBeginLoc()) &&
                                    sm_.isBeforeInTranslationUnit(b.var->getLocation(), loop->getEndLoc());
        const bool isThisRangeFor = !b.var && llvm::isa<CXXForRangeStmt>(loop) && b.declLoc == loop->getBeginLoc();
        return llvm::isa<ContinueStmt>(next) ? declaredInLoop : (declaredInLoop || isThisRangeFor);
      }
      if (mentionsBorrow(next))
        return false;
    }
    return false;
  }

  bool later(SourceLocation a, SourceLocation b) const { return sm_.isBeforeInTranslationUnit(b, a); }

  static const ValueDecl *rootDecl(const Expr *root) {
    if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(root))
      return dre->getDecl();
    return g_thisRoot;
  }

  // Climbs from a root (a variable reference or `this`) through field accesses to the full place expression.
  const Expr *topOfPlace(const Expr *root, Place &out) {
    const Expr *top = root;
    out = Place{rootDecl(root), {}};
    const bool thisRooted = llvm::isa<CXXThisExpr>(root);
    for (;;) {
      const auto *parent = parentOf(top);
      const auto *me = parent ? llvm::dyn_cast<MemberExpr>(parent) : nullptr;
      if (!me || (me->isArrow() && !thisRooted) || strip(me->getBase()) != strip(top))
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
      for (const Expr *dre : refs_) {
        if (rootDecl(dre) != b.var)
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
  std::vector<const Expr *> refs_;
  std::vector<const CompoundStmt *> blocks_;
  std::vector<const Stmt *> loops_;
};

// One call argument together with how the callee binds it.
struct ArgBinding {
  const Expr *expr;
  bool mut;
  Place place;
};

// An index or an unknown pointer makes overlap undecidable: treat those arguments as unrelated.
bool hasUndecidableStep(const Expr *e) {
  for (e = strip(e); e;) {
    if (llvm::isa<ArraySubscriptExpr>(e))
      return true;
    if (const auto *me = llvm::dyn_cast<MemberExpr>(e))
      e = strip(me->getBase());
    else if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e))
      e = strip(uo->getSubExpr());
    else
      return false;
  }
  return false;
}

void addBinding(std::vector<ArgBinding> &out, const Expr *arg, QualType paramType) {
  if (!arg || paramType.isNull() || paramType->isDependentType() || arg->isTypeDependent() || hasUndecidableStep(arg))
    return;
  bool mut = false;
  const Expr *source = arg;
  if (paramType->isLValueReferenceType()) {
    mut = !paramType.getNonReferenceType().isConstQualified();
    // A const reference bound to a temporary names a fresh object: 'v = v.substr(1)' copies the view's value
    // (pointer and size), so the temporary's storage never overlaps the receiver.
    if (!mut && llvm::isa<MaterializeTemporaryExpr>(arg->IgnoreParens()))
      return;
  } else if (paramType->isPointerType()) {
    const Expr *s = strip(arg);
    const auto *uo = llvm::dyn_cast<UnaryOperator>(s);
    if (!uo || uo->getOpcode() != UO_AddrOf)
      return; // only an explicit '&place' names the pointee
    mut = !paramType->getPointeeType().isConstQualified();
    source = uo;
  } else {
    return; // by value (copy) or rvalue reference (move): no aliasing borrow
  }
  if (Place p = placeOf(source))
    out.push_back({arg, mut, std::move(p)});
}

class AliasVisitor : public RecursiveASTVisitor<AliasVisitor> {
public:
  explicit AliasVisitor(std::vector<std::vector<ArgBinding>> &calls) : calls_(calls) {}

  bool VisitCallExpr(CallExpr *call) {
    const FunctionDecl *fd = call->getDirectCallee();
    if (!fd)
      return true;
    std::vector<ArgBinding> args;
    unsigned firstArg = 0;
    const auto *md = llvm::dyn_cast<CXXMethodDecl>(fd);
    if (const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(call)) {
      if (md && !md->isStatic())
        if (const Expr *obj = mc->getImplicitObjectArgument(); obj && !obj->isTypeDependent())
          // An implicit 'this' receiver would conflict with every member argument: 'g(x_)' is idiomatic.
          if (!hasUndecidableStep(obj) && !llvm::isa<CXXThisExpr>(strip(obj)))
            if (Place p = placeOf(obj)) {
              // `ptr_->f()`: the receiver is the pointee, not the pointer member.
              if (obj->getType()->isPointerType())
                p.pointee = !p.pointee;
              args.push_back({obj, !md->isConst(), std::move(p)});
            }
    } else if (llvm::isa<CXXOperatorCallExpr>(call) && md && !md->isStatic() && call->getNumArgs() > 0) {
      const Expr *obj = call->getArg(0);
      if (!obj->isTypeDependent() && !hasUndecidableStep(obj))
        if (Place p = placeOf(obj))
          args.push_back({obj, !md->isConst(), std::move(p)});
      firstArg = 1;
    }
    for (unsigned i = firstArg; i < call->getNumArgs(); ++i) {
      const unsigned pi = i - firstArg;
      if (pi < fd->getNumParams())
        addBinding(args, call->getArg(i), fd->getParamDecl(pi)->getType());
    }
    calls_.push_back(std::move(args));
    return true;
  }

  bool VisitCXXConstructExpr(CXXConstructExpr *ce) {
    const CXXConstructorDecl *ctor = ce->getConstructor();
    std::vector<ArgBinding> args;
    for (unsigned i = 0; i < ce->getNumArgs() && i < ctor->getNumParams(); ++i)
      addBinding(args, ce->getArg(i), ctor->getParamDecl(i)->getType());
    calls_.push_back(std::move(args));
    return true;
  }

private:
  std::vector<std::vector<ArgBinding>> &calls_;
};

// --- reloco-escaping-local ------------------------------------------------------

bool thisBound(const CXXMethodDecl *md, const ASTContext &ctx) {
  if (md->hasAttr<LifetimeBoundAttr>())
    return true;
  for (QualType t = md->getType();;) {
    if (const auto *at = llvm::dyn_cast<AttributedType>(t.getTypePtr()); at && at->getAttrKind() == attr::LifetimeBound)
      return true;
    const QualType next = t.getSingleStepDesugaredType(ctx);
    if (next == t)
      return false;
    t = next;
  }
}

// The automatic variable that owns the storage `e` designates, or null when it may point elsewhere
// (references, pointers, views, dereferences, statics).
const VarDecl *localObjectRoot(const Expr *e) {
  e = strip(e);
  if (!e)
    return nullptr;
  if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e)) {
    const auto *v = llvm::dyn_cast<VarDecl>(dre->getDecl());
    if (!v || !v->hasLocalStorage() || v->isStaticLocal() || v->isImplicit())
      return nullptr;
    const QualType t = v->getType();
    return t->isDependentType() || isAliasingType(t) ? nullptr : v;
  }
  if (const auto *me = llvm::dyn_cast<MemberExpr>(e))
    return me->isArrow() ? nullptr : localObjectRoot(me->getBase());
  if (const auto *as = llvm::dyn_cast<ArraySubscriptExpr>(e))
    return strip(as->getBase())->getType()->isArrayType() ? localObjectRoot(as->getBase()) : nullptr;
  return nullptr;
}

// The local whose storage the value of `e` points into, when `e` is a pointer/view derived from it.
const VarDecl *borrowedLocal(const Expr *e, const ASTContext &ctx) {
  e = strip(e);
  if (!e || e->getType().isNull() || e->getType()->isDependentType())
    return nullptr;
  if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e))
    return uo->getOpcode() == UO_AddrOf ? localObjectRoot(uo->getSubExpr()) : nullptr;
  if (e->getType()->isArrayType()) // decays to a pointer to the first element
    return localObjectRoot(e);
  if (const auto *ce = llvm::dyn_cast<CXXConstructExpr>(e))
    return ce->getNumArgs() > 0 && isAliasingType(ce->getType()) ? borrowedLocal(ce->getArg(0), ctx) : nullptr;
  if (const auto *call = llvm::dyn_cast<CallExpr>(e)) {
    const FunctionDecl *fd = call->getDirectCallee();
    if (!fd || !isAliasingType(fd->getReturnType()) || hasStatic(declReturnLifetimes(fd)))
      return nullptr;
    if (const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(call)) {
      const auto *md = llvm::dyn_cast<CXXMethodDecl>(fd);
      if (md && (thisBound(md, ctx) || sharesLifetime(declThisLifetimes(md), declReturnLifetimes(md))))
        if (const VarDecl *v = localObjectRoot(mc->getImplicitObjectArgument()))
          return v;
    }
    const unsigned skip = llvm::isa<CXXOperatorCallExpr>(call) && llvm::isa<CXXMethodDecl>(fd) ? 1u : 0u;
    for (unsigned i = skip; i < call->getNumArgs() && i - skip < fd->getNumParams(); ++i)
      if (paramFlowsToReturn(fd, i - skip))
        if (const VarDecl *v = localObjectRoot(call->getArg(i)))
          return v;
  }
  return nullptr;
}

// The parameter through which an assignment target reaches caller-owned storage, or null.
const ParmVarDecl *callerStorageParam(const Expr *e, bool indirect) {
  e = strip(e);
  if (!e)
    return nullptr;
  if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e)) {
    const auto *p = llvm::dyn_cast<ParmVarDecl>(dre->getDecl());
    if (!p)
      return nullptr;
    const QualType t = p->getType();
    if (t->isLValueReferenceType() && !t.getNonReferenceType().isConstQualified())
      return p; // a non-const reference parameter is caller storage itself
    return indirect && t->isPointerType() ? p : nullptr;
  }
  if (const auto *me = llvm::dyn_cast<MemberExpr>(e))
    return callerStorageParam(me->getBase(), indirect || me->isArrow());
  if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e))
    return uo->getOpcode() == UO_Deref ? callerStorageParam(uo->getSubExpr(), true) : nullptr;
  if (const auto *as = llvm::dyn_cast<ArraySubscriptExpr>(e))
    return callerStorageParam(as->getBase(), true);
  return nullptr;
}

class EscapeVisitor : public RecursiveASTVisitor<EscapeVisitor> {
public:
  struct Hit {
    const Expr *rhs;
    const VarDecl *local;
    const ParmVarDecl *param;
  };

  explicit EscapeVisitor(ASTContext &ctx) : ctx_(ctx) {}
  bool shouldVisitTemplateInstantiations() const { return false; }

  bool VisitBinaryOperator(BinaryOperator *bo) {
    if (bo->getOpcode() == BO_Assign)
      consider(bo->getLHS(), bo->getRHS());
    return true;
  }

  bool VisitCXXOperatorCallExpr(CXXOperatorCallExpr *oc) {
    if (oc->getOperator() == OO_Equal && oc->getNumArgs() == 2)
      consider(oc->getArg(0), oc->getArg(1));
    return true;
  }

  std::vector<Hit> hits;

private:
  void consider(const Expr *lhs, const Expr *rhs) {
    const QualType lt = lhs->getType();
    if (lt.isNull() || lt->isDependentType() || lt->isReferenceType() || !isAliasingType(lt))
      return;
    const ParmVarDecl *param = callerStorageParam(lhs, false);
    if (!param)
      return;
    if (const VarDecl *local = borrowedLocal(rhs, ctx_))
      hits.push_back({rhs, local, param});
  }

  ASTContext &ctx_;
};

// --- reloco-lifetime-names ------------------------------------------------------

// The caller-owned or local object `e` borrows from when it is not of static duration, or null when `e` is static
// (string literal, global, function, `RELOCO_LT(static)` parameter) or its origin is unknown.
const NamedDecl *nonStaticOrigin(const Expr *e, bool byRef, const ASTContext &ctx) {
  e = strip(e);
  if (!e || e->isTypeDependent() || e->getType().isNull())
    return nullptr;
  if (const VarDecl *v = borrowedLocal(e, ctx))
    return v;
  if (byRef)
    if (const VarDecl *v = localObjectRoot(e))
      return v;
  if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e))
    if (const auto *p = llvm::dyn_cast<ParmVarDecl>(dre->getDecl()))
      if (isAliasingType(p->getType()) && !hasStatic(paramLifetimes(p)))
        return p;
  return nullptr;
}

class NamesVisitor : public RecursiveASTVisitor<NamesVisitor> {
public:
  struct Finding {
    const Expr *expr;
    const NamedDecl *origin;
    const FunctionDecl *callee; // null for a return statement or a store
    unsigned param;
    bool isStatic;
    const ValueDecl *target = nullptr; // the labelled field or variable a value is stored into
  };

  NamesVisitor(const FunctionDecl *fn, ASTContext &ctx) : fn_(fn), ctx_(ctx) {
    if (!fn)
      return; // initialisers of fields and variables outside any function
    ret_ = declReturnLifetimes(fn);
    this_ = llvm::isa<CXXMethodDecl>(fn) ? declThisLifetimes(fn) : LifetimeSet{};
  }

  bool shouldVisitTemplateInstantiations() const { return false; }
  bool TraverseLambdaExpr(LambdaExpr *) { return true; } // its returns belong to the lambda

  bool VisitReturnStmt(ReturnStmt *rs) {
    const Expr *v = rs->getRetValue();
    const QualType rt = fn_->getReturnType();
    if (ret_.empty() || !v || rt.isNull() || rt->isDependentType() || !isAliasingType(rt))
      return true;
    const bool byRef = rt->isReferenceType();
    const bool onlyStatic = std::all_of(ret_.begin(), ret_.end(), [](const std::string &n) { return n == "static"; });
    if (onlyStatic) {
      if (const NamedDecl *o = nonStaticOrigin(v, byRef, ctx_))
        findings.push_back({v, o, nullptr, 0, true});
      return true;
    }
    // A field tagged with a returned lifetime hands that lifetime out: `RELOCO_LT(a) const T *p_; ... return p_;`.
    if (const auto *me = llvm::dyn_cast<MemberExpr>(strip(v)))
      if (const auto *fd = llvm::dyn_cast<FieldDecl>(me->getMemberDecl()); fd && sharesLifetime(declLifetimes(fd), ret_))
        return true;
    Place pl = placeOfPointerRead(v);
    if (!pl)
      return true;
    if (const auto *p = llvm::dyn_cast<ParmVarDecl>(pl.var)) {
      if (isAliasingType(p->getType()) && !hasStatic(paramLifetimes(p)) && !sharesLifetime(declParamLifetimes(fn_, p->getFunctionScopeIndex()), ret_))
        findings.push_back({v, p, nullptr, 0, false});
    } else if (isThisRoot(pl.var) && !sharesLifetime(this_, ret_)) {
      findings.push_back({v, fn_, nullptr, 0, false});
    }
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *bo) {
    if (bo->getOpcode() == BO_Assign)
      checkStore(bo->getLHS(), bo->getRHS());
    return true;
  }

  bool VisitCallExpr(CallExpr *call) {
    const FunctionDecl *fd = call->getDirectCallee();
    if (!fd)
      return true;
    if (const auto *oc = llvm::dyn_cast<CXXOperatorCallExpr>(call); oc && oc->getOperator() == OO_Equal && oc->getNumArgs() == 2)
      checkStore(oc->getArg(0), oc->getArg(1));
    checkArgs(call, fd, llvm::isa<CXXOperatorCallExpr>(call) && llvm::isa<CXXMethodDecl>(fd) ? 1u : 0u);
    return true;
  }
  bool VisitCXXConstructExpr(CXXConstructExpr *ce) {
    checkArgs(ce, ce->getConstructor(), 0);
    return true;
  }

  std::vector<Finding> findings;

  // `value` is stored into the labelled field or variable `target`.
  void checkValueStore(const ValueDecl *target, const Expr *value) {
    const LifetimeSet want = declLifetimes(target);
    const QualType tt = target->getType();
    if (want.empty() || !value || value->isTypeDependent() || tt->isDependentType())
      return;
    const bool byRef = tt->isReferenceType();
    if (hasStatic(want)) {
      if (const NamedDecl *o = nonStaticOrigin(value, byRef, ctx_))
        findings.push_back({value, o, nullptr, 0, true, target});
      return;
    }
    if (const VarDecl *local = borrowedLocal(value, ctx_)) {
      findings.push_back({value, local, nullptr, 0, false, target});
      return;
    }
    const Expr *e = strip(value);
    if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e))
      if (const auto *p = llvm::dyn_cast<ParmVarDecl>(dre->getDecl()); p && isAliasingType(p->getType())) {
        const LifetimeSet have = fn_ ? declParamLifetimes(fn_, p->getFunctionScopeIndex()) : LifetimeSet{};
        if (!hasStatic(have) && !sharesLifetime(have, want))
          findings.push_back({value, p, nullptr, 0, false, target});
      }
  }

private:
  void checkStore(const Expr *lhs, const Expr *rhs) {
    if (const auto *me = llvm::dyn_cast<MemberExpr>(strip(lhs)))
      if (const auto *fd = llvm::dyn_cast<FieldDecl>(me->getMemberDecl()))
        checkValueStore(fd, rhs);
  }

  template <class C> void checkArgs(C *call, const FunctionDecl *fd, unsigned skip) {
    for (unsigned i = skip; i < call->getNumArgs() && i - skip < fd->getNumParams(); ++i) {
      if (!hasStatic(declParamLifetimes(fd, i - skip)))
        continue;
      const Expr *arg = call->getArg(i);
      const bool byRef = fd->getParamDecl(i - skip)->getType()->isReferenceType();
      if (const NamedDecl *o = nonStaticOrigin(arg, byRef, ctx_))
        findings.push_back({arg, o, fd, i - skip, true});
    }
  }

  const FunctionDecl *fn_;
  ASTContext &ctx_;
  LifetimeSet ret_, this_;
};

bool hasExemptAnnotation(const Decl *d) {
  while (d) {
    for (const auto *an : d->specific_attrs<AnnotateAttr>())
      if (an->getAnnotation() == "reloco_borrow_exempt")
        return true;
    if (llvm::isa<TranslationUnitDecl>(d))
      break;
    // A lambda inside a variable template initialiser can have no enclosing context.
    const DeclContext *dc = d->getDeclContext();
    d = dc ? Decl::castFromDeclContext(dc) : nullptr;
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
  const auto *method = llvm::dyn_cast<CXXMethodDecl>(fn);
  g_thisRoot = method && !method->isStatic() ? method : nullptr;
  an.TraverseStmt(fn->getBody());

  const SourceManager &sm = *result.SourceManager;
  an.finish([&](const Expr *use, llvm::StringRef name, const Borrow &b, Access a) {
    if (exempt(sm, use->getBeginLoc()) || exempt(sm, b.declLoc))
      return;
    const llvm::StringRef what = a == Access::read ? "use" : a == Access::mutate ? "mutation" : "mutable borrow";
    const std::string borrower = b.var ? "'" + b.var->getName().str() + "'" : "the enclosing range-for loop";
    diag(use->getBeginLoc(), "%0 of '%1' while it is %2 borrowed by %3")
        << what << name << (b.mut ? "mutably" : "shared") << borrower;
    diag(b.declLoc, "borrow created here", DiagnosticIDs::Note);
    if (b.var)
      diag(b.lastUse, "borrow is still used here", DiagnosticIDs::Note);
    howToFix(use->getBeginLoc(), "end the borrow first (copy the value out: 'int v = r;' and use v later), reorder so the "
                                 "last use of the borrow comes before this access, or borrow disjoint fields. If the "
                                 "aliasing is sound, mark the function RELOCO_BORROW_EXEMPT with a comment");
  });
  g_thisRoot = nullptr;
}

void AliasedArgumentsCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit()), unless(isTemplateInstantiation())).bind("fn"),
                     this);
}

void AliasedArgumentsCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->hasBody() || fn->isTemplateInstantiation() || hasExemptAnnotation(fn) || inUnsafeTier(fn) ||
      exempt(*result.SourceManager, fn->getLocation()))
    return;
  if (const auto *md = llvm::dyn_cast<CXXMethodDecl>(fn); md && md->getParent()->isLambda())
    return;

  const auto *method = llvm::dyn_cast<CXXMethodDecl>(fn);
  g_thisRoot = method && !method->isStatic() ? method : nullptr;
  std::vector<std::vector<ArgBinding>> calls;
  AliasVisitor visitor(calls);
  visitor.TraverseStmt(fn->getBody());

  const SourceManager &sm = *result.SourceManager;
  for (const auto &args : calls) {
    for (size_t j = 1; j < args.size(); ++j) {
      for (size_t i = 0; i < j; ++i) {
        if ((!args[i].mut && !args[j].mut) || !args[i].place.overlaps(args[j].place))
          continue;
        if (exempt(sm, args[j].expr->getBeginLoc()))
          continue;
        diag(args[j].expr->getBeginLoc(), "argument aliases another argument of the same call that is passed as a "
                                          "mutable reference/pointer (%0 vs %1)")
            << (args[j].mut ? "mutable" : "shared") << (args[i].mut ? "mutable" : "shared");
        diag(args[i].expr->getBeginLoc(), "the other argument is here", DiagnosticIDs::Note);
        howToFix(args[j].expr->getBeginLoc(),
                 "pass a copy for the read-only argument ('int v = x.a; f(x, v);'), split the call, or use a "
                 "single parameter; if the callee supports aliasing, mark the function RELOCO_BORROW_EXEMPT with a "
                 "comment");
        break;
      }
    }
  }
  g_thisRoot = nullptr;
}

void LifetimeNamesCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(unless(isImplicit()), unless(isTemplateInstantiation())).bind("fn"), this);
  finder->addMatcher(fieldDecl(unless(isImplicit())).bind("field"), this);
  finder->addMatcher(varDecl(hasInitializer(expr().bind("value")), unless(parmVarDecl()), unless(isImplicit())).bind("var"),
                     this);
}

void LifetimeNamesCheck::check(const MatchFinder::MatchResult &result) {
  const SourceManager &sourceMgr = *result.SourceManager;
  auto reportStore = [&](const NamesVisitor::Finding &h) {
    if (exempt(sourceMgr, h.expr->getBeginLoc()))
      return;
    const char *kind = llvm::isa<ParmVarDecl>(h.origin) ? "parameter" : "local";
    if (h.isStatic) {
      diag(h.expr->getBeginLoc(), "'%0' is RELOCO_LT(static) but is given a borrow of %1 '%2'") << h.target << kind << h.origin;
      howToFix(h.expr->getBeginLoc(), "store a string literal or a global/static object, or tag the source RELOCO_LT(static)");
      return;
    }
    std::string names;
    for (const std::string &n : declLifetimes(h.target))
      names += (names.empty() ? "" : ", ") + n;
    diag(h.expr->getBeginLoc(), "'%0' has lifetime '%1' but is given a borrow of %2 '%3' that does not carry it")
        << h.target << names << kind << h.origin;
    howToFix(h.expr->getBeginLoc(), "tag the parameter with the same RELOCO_LT lifetime as the field, or store something "
                                    "that outlives it");
  };
  // Declarations of labelled fields, and initialisers of labelled fields and variables.
  if (const auto *field = result.Nodes.getNodeAs<FieldDecl>("field")) {
    const LifetimeSet want = declLifetimes(field);
    if (want.empty() || field->getParent()->isLambda() || exempt(sourceMgr, field->getLocation()) || inUnsafeTier(field))
      return;
    const QualType t = field->getType();
    if (!t->isDependentType() && !isAliasingType(t))
      diag(field->getLocation(), "RELOCO_LT on field '%0', which is neither a reference, pointer nor view") << field;
    if (field->hasInClassInitializer()) {
      NamesVisitor v(nullptr, *result.Context);
      v.checkValueStore(field, field->getInClassInitializer());
      for (const auto &h : v.findings)
        reportStore(h);
    }
    return;
  }
  if (const auto *var = result.Nodes.getNodeAs<VarDecl>("var")) {
    if (declLifetimes(var).empty() || exempt(sourceMgr, var->getLocation()) || inUnsafeTier(var))
      return;
    NamesVisitor v(nullptr, *result.Context);
    v.checkValueStore(var, var->getInit());
    for (const auto &h : v.findings)
      reportStore(h);
    return;
  }

  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || fn->isTemplateInstantiation() || inUnsafeTier(fn) || exempt(*result.SourceManager, fn->getLocation()))
    return;
  if (const auto *md = llvm::dyn_cast<CXXMethodDecl>(fn); md && md->getParent()->isLambda())
    return;

  // A constructor must give every labelled field a value, otherwise the field holds a borrow nobody checked.
  // Classes whose storage is legitimately left uninitialised opt out with RELOCO_BORROW_EXEMPT on the field, the
  // class or the constructor.
  if (const auto *ctor = llvm::dyn_cast<CXXConstructorDecl>(fn);
      ctor && ctor->doesThisDeclarationHaveABody() && !ctor->isDelegatingConstructor() &&
      !ctor->isCopyOrMoveConstructor() && !ctor->getParent()->isUnion() && !hasExemptAnnotation(ctor)) {
    for (const FieldDecl *f : ctor->getParent()->fields()) {
      if (declLifetimes(f).empty() || f->hasInClassInitializer() || f->isUnnamedBitField() || hasExemptAnnotation(f))
        continue;
      const bool set = std::any_of(ctor->init_begin(), ctor->init_end(), [&](const CXXCtorInitializer *i) {
        return i->isWritten() && i->getMember() == f;
      });
      if (!set) {
        diag(ctor->getLocation(), "constructor does not initialise the lifetime-labelled field '%0'") << f;
        howToFix(ctor->getLocation(), "initialise it in the member initialiser list or with a default member initialiser; if "
                                      "the storage is intentionally left uninitialised, mark the field, the class or this "
                                      "constructor RELOCO_BORROW_EXEMPT with a comment");
      }
    }
  }

  // Declaration rules, reported once on the canonical declaration (annotations of all redeclarations are merged).
  if (fn->getCanonicalDecl() == fn) {
    const LifetimeSet ret = declReturnLifetimes(fn);
    const LifetimeSet self = declThisLifetimes(fn);
    const QualType rt = fn->getReturnType();
    if (!ret.empty() && !rt->isDependentType() && !isAliasingType(rt))
      diag(fn->getLocation(), "RELOCO_LT_RETURN on '%0', which does not return a reference, pointer or view") << fn;
    for (const std::string &n : ret) {
      if (n == "static")
        continue;
      bool carried = sharesLifetime(self, {n});
      for (unsigned i = 0; i < fn->getNumParams() && !carried; ++i)
        carried = sharesLifetime(declParamLifetimes(fn, i), {n});
      if (const auto *md = llvm::dyn_cast<CXXMethodDecl>(fn); md && !md->isStatic())
        for (const FieldDecl *f : md->getParent()->fields())
          carried = carried || sharesLifetime(declLifetimes(f), {n});
      if (!carried)
        diag(fn->getLocation(), "RELOCO_LT_RETURN(%0) names a lifetime that no parameter, field or RELOCO_LT_THIS carries")
            << n;
    }
    for (const ParmVarDecl *p : fn->parameters()) {
      const QualType t = p->getType();
      if (!paramLifetimes(p).empty() && !t->isDependentType() && !isAliasingType(t))
        diag(p->getLocation(), "RELOCO_LT on parameter '%0', which is neither a reference, pointer nor view") << p;
    }
    if (!self.empty() && !llvm::isa<CXXMethodDecl>(fn))
      diag(fn->getLocation(), "RELOCO_LT_THIS on '%0', which is not a member function") << fn;
  }

  if (!fn->isThisDeclarationADefinition() || !fn->hasBody() || hasExemptAnnotation(fn))
    return;
  const auto *method = llvm::dyn_cast<CXXMethodDecl>(fn);
  g_thisRoot = method && !method->isStatic() ? method : nullptr;
  NamesVisitor visitor(fn, *result.Context);
  if (const auto *ctor = llvm::dyn_cast<CXXConstructorDecl>(fn))
    for (const CXXCtorInitializer *init : ctor->inits())
      if (init->isMemberInitializer() && !init->isInClassMemberInitializer())
        visitor.checkValueStore(init->getMember(), init->getInit());
  visitor.TraverseStmt(fn->getBody());
  g_thisRoot = nullptr;

  const SourceManager &sm = *result.SourceManager;
  for (const auto &h : visitor.findings) {
    if (exempt(sm, h.expr->getBeginLoc()))
      continue;
    if (h.target) {
      reportStore(h);
    } else if (h.callee) {
      diag(h.expr->getBeginLoc(), "argument %0 of '%1' must be 'static (RELOCO_LT(static)) but borrows %2 '%3'")
          << (h.param + 1) << h.callee << (llvm::isa<ParmVarDecl>(h.origin) ? "parameter" : "local") << h.origin;
      howToFix(h.expr->getBeginLoc(), "pass a string literal, a global/static object or a function; to forward a parameter, "
                                      "declare it RELOCO_LT(static) in this function too");
    } else if (h.isStatic) {
      diag(h.expr->getBeginLoc(), "'%0' is declared to return a 'static lifetime (RELOCO_LT_RETURN(static)) but returns "
                                  "a borrow of %1 '%2'")
          << fn << (llvm::isa<ParmVarDecl>(h.origin) ? "parameter" : "local") << h.origin;
      howToFix(h.expr->getBeginLoc(), "return a string literal or a global/static object, or tag the parameter "
                                      "RELOCO_LT(static)");
    } else {
      diag(h.expr->getBeginLoc(), "return value borrows %0, which does not carry a lifetime named by RELOCO_LT_RETURN")
          << (llvm::isa<ParmVarDecl>(h.origin) ? "parameter '" + h.origin->getNameAsString() + "'" : std::string("'this'"));
      howToFix(h.expr->getBeginLoc(), "tag the parameter (or the method with RELOCO_LT_THIS) with a lifetime listed in "
                                      "RELOCO_LT_RETURN, or return something it does not borrow");
    }
  }
}

void EscapingLocalCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit()), unless(isTemplateInstantiation())).bind("fn"),
                     this);
}

void EscapingLocalCheck::check(const MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->hasBody() || fn->isTemplateInstantiation() || hasExemptAnnotation(fn) || inUnsafeTier(fn) ||
      exempt(*result.SourceManager, fn->getLocation()))
    return;
  EscapeVisitor visitor(*result.Context);
  visitor.TraverseStmt(fn->getBody());
  for (const auto &h : visitor.hits) {
    if (exempt(*result.SourceManager, h.rhs->getBeginLoc()))
      continue;
    diag(h.rhs->getBeginLoc(), "pointer into local '%0' is stored through parameter '%1' and dangles when the "
                               "function returns")
        << h.local << h.param;
    diag(h.local->getLocation(), "local declared here", DiagnosticIDs::Note);
    howToFix(h.rhs->getBeginLoc(), "return the value instead, let the caller provide the storage (take it as a "
                                   "parameter), or allocate it with an allocator the caller owns; if the pointee "
                                   "really outlives the call, mark the function RELOCO_BORROW_EXEMPT with a comment");
  }
}

} // namespace reloco::tidy
