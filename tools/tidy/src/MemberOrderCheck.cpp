// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/RecursiveASTVisitor.h"

#include <algorithm>
#include <set>
#include <vector>

namespace reloco::tidy {

namespace {

bool isAliasing(QualType t) {
  if (t.isNull() || t->isDependentType())
    return false;
  if (t->isReferenceType() || t->isPointerType())
    return true;
  const CXXRecordDecl *rd = t.getCanonicalType()->getAsCXXRecordDecl();
  return rd && rd->hasDefinition() && rd->hasAttr<PointerAttr>();
}

const Expr *strip(const Expr *e) {
  while (e) {
    const Expr *next = e->IgnoreParenImpCasts();
    if (const auto *m = llvm::dyn_cast<MaterializeTemporaryExpr>(next))
      next = m->getSubExpr();
    else if (const auto *b = llvm::dyn_cast<CXXBindTemporaryExpr>(next))
      next = b->getSubExpr();
    else if (const auto *c = llvm::dyn_cast<ExprWithCleanups>(next))
      next = c->getSubExpr();
    if (next == e)
      return e;
    e = next;
  }
  return e;
}

// The direct field of `rd` that `e` designates (through any depth of member access), or null.
const FieldDecl *thisField(const Expr *e, const CXXRecordDecl *rd) {
  const FieldDecl *found = nullptr;
  for (e = strip(e); e;) {
    const auto *me = llvm::dyn_cast<MemberExpr>(e);
    if (!me)
      return nullptr;
    const auto *fd = llvm::dyn_cast<FieldDecl>(me->getMemberDecl());
    const Expr *base = strip(me->getBase());
    if (!fd)
      return nullptr;
    if (llvm::isa<CXXThisExpr>(base))
      return fd->getParent()->getCanonicalDecl() == rd->getCanonicalDecl() ? fd : found;
    if (me->isArrow())
      return nullptr; // goes through a pointer: not storage of this object
    found = nullptr;
    e = base;
  }
  return nullptr;
}

// The field of this object whose storage the value of `e` points into, when `e` binds/derives from it.
const FieldDecl *selfBorrow(const Expr *e, const CXXRecordDecl *rd, ASTContext &ctx) {
  e = strip(e);
  if (!e || e->getType().isNull() || e->getType()->isDependentType())
    return nullptr;
  if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e))
    return uo->getOpcode() == UO_AddrOf ? thisField(uo->getSubExpr(), rd) : nullptr;
  if (e->getType()->isArrayType() || llvm::isa<MemberExpr>(e))
    return thisField(e, rd); // array decay, or a reference/view bound to the member directly
  if (const auto *ce = llvm::dyn_cast<CXXConstructExpr>(e))
    return ce->getNumArgs() > 0 && isAliasing(ce->getType()) ? selfBorrow(ce->getArg(0), rd, ctx) : nullptr;
  if (const auto *call = llvm::dyn_cast<CallExpr>(e)) {
    const FunctionDecl *fd = call->getDirectCallee();
    if (!fd || !isAliasing(fd->getReturnType()))
      return nullptr;
    if (const auto *mc = llvm::dyn_cast<CXXMemberCallExpr>(call)) {
      // Only accessors that return storage of the object they are called on.
      const auto *md = mc->getMethodDecl();
      if (md && md->hasAttr<LifetimeBoundAttr>())
        return thisField(mc->getImplicitObjectArgument(), rd);
      for (QualType t = md ? md->getType() : QualType(); !t.isNull();) {
        if (const auto *at = llvm::dyn_cast<AttributedType>(t.getTypePtr()); at && at->getAttrKind() == attr::LifetimeBound)
          return thisField(mc->getImplicitObjectArgument(), rd);
        const QualType next = t.getSingleStepDesugaredType(ctx);
        if (next == t)
          break;
        t = next;
      }
    }
    for (unsigned i = 0; i < call->getNumArgs() && i < fd->getNumParams(); ++i)
      if (fd->getParamDecl(i)->hasAttr<LifetimeBoundAttr>())
        if (const FieldDecl *f = selfBorrow(call->getArg(i), rd, ctx))
          return f;
  }
  return nullptr;
}

// Every field of this object mentioned by an initialiser, and whether only its address is taken.
class FieldRefCollector : public RecursiveASTVisitor<FieldRefCollector> {
public:
  struct Ref {
    const FieldDecl *field;
    const Expr *expr;
    bool addressOnly;
  };

  explicit FieldRefCollector(const CXXRecordDecl *rd) : rd_(rd) {}
  bool shouldVisitTemplateInstantiations() const { return false; }

  bool TraverseUnaryOperator(UnaryOperator *uo, DataRecursionQueue *q = nullptr) {
    const bool addr = uo->getOpcode() == UO_AddrOf;
    addr_ += addr ? 1 : 0;
    const bool r = RecursiveASTVisitor::TraverseUnaryOperator(uo, q);
    addr_ -= addr ? 1 : 0;
    return r;
  }

  bool TraverseImplicitCastExpr(ImplicitCastExpr *ic, DataRecursionQueue *q = nullptr) {
    const bool decay = ic->getCastKind() == CK_ArrayToPointerDecay;
    addr_ += decay ? 1 : 0;
    const bool r = RecursiveASTVisitor::TraverseImplicitCastExpr(ic, q);
    addr_ -= decay ? 1 : 0;
    return r;
  }

  bool TraverseMemberExpr(MemberExpr *me, DataRecursionQueue *q = nullptr) {
    if (const FieldDecl *f = thisField(me, rd_)) {
      refs.push_back({f, me, addr_ > 0});
      return true; // do not descend: the outermost member access is the use
    }
    return RecursiveASTVisitor::TraverseMemberExpr(me, q);
  }

  std::vector<Ref> refs;

private:
  const CXXRecordDecl *rd_;
  int addr_ = 0;
};

unsigned fieldIndex(const CXXRecordDecl *rd, const FieldDecl *f) {
  unsigned i = 0;
  for (const FieldDecl *x : rd->fields()) {
    if (x->getCanonicalDecl() == f->getCanonicalDecl())
      return i;
    ++i;
  }
  return i;
}

bool hasNontrivialDtor(QualType t) {
  const CXXRecordDecl *rd = t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl();
  return rd && rd->hasDefinition() && !rd->hasTrivialDestructor();
}

// A copy or move that would be generated for the class (not deleted, not hand-written).
bool copiesBlindly(const CXXRecordDecl *rd) {
  bool copyDeclared = false, moveDeclared = false, bad = false;
  for (const CXXConstructorDecl *c : rd->ctors()) {
    if (c->isCopyConstructor()) {
      copyDeclared = true;
      bad |= c->isDefaulted() && !c->isDeleted();
    } else if (c->isMoveConstructor()) {
      moveDeclared = true;
      bad |= c->isDefaulted() && !c->isDeleted();
    }
  }
  if (!copyDeclared && rd->needsImplicitCopyConstructor() && !rd->defaultedCopyConstructorIsDeleted())
    bad = true;
  if (!moveDeclared && rd->needsImplicitMoveConstructor() && !rd->defaultedMoveConstructorIsDeleted())
    bad = true;
  return bad;
}

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

struct InitInfo {
  const FieldDecl *field;
  const Expr *init;
};

} // namespace

void MemberOrderCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit()), unless(isLambda()), unless(isUnion()),
                                   unless(isTemplateInstantiation()))
                         .bind("rec"),
                     this);
}

void MemberOrderCheck::check(const MatchFinder::MatchResult &result) {
  const auto *rd = result.Nodes.getNodeAs<CXXRecordDecl>("rec");
  if (!rd || !rd->isThisDeclarationADefinition() || rd->isInvalidDecl() || hasExemptAnnotation(rd) ||
      inUnsafeTier(rd) || exempt(*result.SourceManager, rd->getLocation()))
    return;
  ASTContext &ctx = *result.Context;

  std::vector<InitInfo> inits;
  for (const FieldDecl *f : rd->fields())
    if (f->hasInClassInitializer())
      inits.push_back({f, f->getInClassInitializer()});
  std::set<const FunctionDecl *> seen;
  for (const CXXConstructorDecl *c : rd->ctors()) {
    const FunctionDecl *def = nullptr;
    if (!c->hasBody(def) || !seen.insert(def).second)
      continue;
    const auto *ctor = llvm::dyn_cast<CXXConstructorDecl>(def);
    if (!ctor || ctor->isDelegatingConstructor() || ctor->isImplicit() || ctor->isDefaulted())
      continue;
    for (const CXXCtorInitializer *ci : ctor->inits())
      if (ci->isWritten() && ci->getMember())
        inits.push_back({ci->getMember(), ci->getInit()});
  }

  const SourceManager &sm = *result.SourceManager;
  std::set<std::pair<const FieldDecl *, const FieldDecl *>> reportedOrder, reportedSelf;
  for (const InitInfo &ii : inits) {
    if (!ii.init || ii.init->getBeginLoc().isInvalid() || exempt(sm, ii.init->getBeginLoc()))
      continue;
    const unsigned own = fieldIndex(rd, ii.field);

    FieldRefCollector refs(rd);
    refs.TraverseStmt(const_cast<Expr *>(ii.init));
    for (const auto &r : refs.refs) {
      if (r.field == ii.field || fieldIndex(rd, r.field) <= own)
        continue;
      const bool destroyedFirst = r.addressOnly && hasNontrivialDtor(ii.field->getType());
      if (r.addressOnly && !destroyedFirst)
        continue; // only the address of a later member is taken and nothing runs on it at destruction
      if (!reportedOrder.insert({ii.field, r.field}).second)
        continue;
      diag(r.expr->getBeginLoc(), destroyedFirst
                                      ? "member %0 refers to %1, which is declared after it and therefore destroyed "
                                        "before it; %0's destructor would use a destroyed object"
                                      : "member %0 is initialised from %1, which is declared after it and not yet "
                                        "constructed at this point")
          << ii.field << r.field;
      diag(r.field->getLocation(), "%0 is declared here", DiagnosticIDs::Note) << r.field;
      howToFix(r.expr->getBeginLoc(), "declare the member that is used before the member that uses it (members are "
                                      "constructed in declaration order and destroyed in reverse), or initialise "
                                      "from a constructor parameter instead");
    }

    if (!isAliasing(ii.field->getType()))
      continue;
    const FieldDecl *target = selfBorrow(ii.init, rd, ctx);
    if (!target || target == ii.field || !copiesBlindly(rd) || !reportedSelf.insert({ii.field, target}).second)
      continue;
    diag(ii.init->getBeginLoc(), "member %0 points into member %1 of the same object, so a copy or move leaves it "
                                 "pointing at the old object (reloco types are also relocated bytewise)")
        << ii.field << target;
    diag(ii.field->getLocation(), "%0 is declared here", DiagnosticIDs::Note) << ii.field;
    howToFix(ii.init->getBeginLoc(), "delete the copy and move constructors and assignments ('T(const T&) = delete;'), "
                                     "store an offset or index instead of a pointer, or write copy/move constructors "
                                     "that re-point it; if relocation is handled elsewhere, suppress with a NOLINT reason");
  }
}

} // namespace reloco::tidy
