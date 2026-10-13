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

// The method whose `this` roots places in the function being analysed (null outside methods).
const ValueDecl *g_thisRoot = nullptr;

bool isThisRoot(const ValueDecl *v) { return llvm::isa_and_nonnull<FunctionDecl>(v); }

bool rootIsConst(const ValueDecl *v) {
  if (const auto *md = llvm::dyn_cast_or_null<CXXMethodDecl>(v))
    return md->isConst();
  return v && v->getType().getNonReferenceType().isConstQualified();
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
    if (ce->getNumArgs() > 0 && isAliasingType(ce->getType()) && !ce->getConstructor()->isCopyOrMoveConstructor())
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
  using Reporter = std::function<void(const Expr *, llvm::StringRef, const Borrow &, Access)>;

  Analyzer(ASTContext &ctx) : ctx_(ctx), sm_(ctx.getSourceManager()) {}

  bool shouldVisitTemplateInstantiations() const { return false; }

  bool VisitVarDecl(VarDecl *vd) {
    if (!isTrackedVar(vd) || llvm::isa<ParmVarDecl>(vd) || !vd->hasInit())
      return true;
    const QualType t = vd->getType();
    if (t.isNull() || t->isDependentType() || !isAliasingType(t))
      return true;
    // Copying a pointer or view out of another object copies its value; it does not borrow that object.
    if (const auto *ic = llvm::dyn_cast<ImplicitCastExpr>(vd->getInit()->IgnoreParens());
        ic && ic->getCastKind() == CK_LValueToRValue)
      return true;
    Place p = placeOf(vd->getInit());
    if (!p || p.var == vd)
      return true;
    // 'auto *self = this;' is a view of the whole object, not a borrow of one member.
    if (isThisRoot(p.var) && p.path.empty() && llvm::isa<CXXThisExpr>(strip(vd->getInit())))
      return true;
    Borrow b;
    b.var = vd;
    b.place = std::move(p);
    b.mut = isMutableTarget(t) && !rootIsConst(b.place.var);
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
        report(root, name, b, a);
      }
  }


private:
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
    if (!fd || !isAliasingType(fd->getReturnType()))
      return nullptr;
    if (const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(call)) {
      const auto *md = llvm::dyn_cast<CXXMethodDecl>(fd);
      if (md && thisBound(md, ctx))
        if (const VarDecl *v = localObjectRoot(mc->getImplicitObjectArgument()))
          return v;
    }
    const unsigned skip = llvm::isa<CXXOperatorCallExpr>(call) && llvm::isa<CXXMethodDecl>(fd) ? 1u : 0u;
    for (unsigned i = skip; i < call->getNumArgs() && i - skip < fd->getNumParams(); ++i)
      if (fd->getParamDecl(i - skip)->hasAttr<LifetimeBoundAttr>())
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
