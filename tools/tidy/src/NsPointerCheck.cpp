// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-ns-pointer: pointers tagged RELOCO_NS(tag) live in a foreign address space (user memory, VM guest memory).
// They may not be dereferenced, mixed with other tags or plain pointers, or cast, except inside a
// RELOCO_NS_TRANSLATOR function.

#include "Checks.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"

#include <string>

namespace reloco::tidy {

namespace {

bool hasAnnotation(const Decl *d, llvm::StringRef name) {
  for (const Decl *r : d->redecls())
    for (const auto *a : r->specific_attrs<AnnotateAttr>())
      if (a->getAnnotation() == name)
        return true;
  return false;
}

// The address-space tag carried by a pointer type ("" when it is a normal pointer).
std::string tagOf(QualType t, const ASTContext &ctx) {
  for (int guard = 0; guard < 32 && !t.isNull(); ++guard) {
    if (const auto *at = llvm::dyn_cast<AttributedType>(t.getTypePtr())) {
      if (const auto *an = llvm::dyn_cast_or_null<AnnotateTypeAttr>(RELOCO_TYPE_ATTR(at)); an && an->getAnnotation() == "reloco_ns" &&
                                                                                    an->args_size() == 1)
        if (const auto *sl = llvm::dyn_cast<StringLiteral>((*an->args_begin())->IgnoreParenImpCasts()))
          return sl->getString().str();
    }
    const QualType next = t.getSingleStepDesugaredType(ctx);
    if (next == t)
      break;
    t = next;
  }
  return {};
}

// The tag of a pointer-valued expression, found from its declaration because implicit conversions drop type sugar.
std::string exprTag(const Expr *e, const ASTContext &ctx) {
  if (!e)
    return {};
  e = e->IgnoreParens();
  if (const auto *ic = llvm::dyn_cast<ImplicitCastExpr>(e))
    return exprTag(ic->getSubExpr(), ctx);
  if (const auto *ec = llvm::dyn_cast<ExplicitCastExpr>(e))
    return tagOf(ec->getTypeAsWritten(), ctx);
  if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e))
    return tagOf(dre->getDecl()->getType(), ctx);
  if (const auto *me = llvm::dyn_cast<MemberExpr>(e))
    return tagOf(me->getMemberDecl()->getType(), ctx);
  if (const auto *call = llvm::dyn_cast<CallExpr>(e))
    return call->getDirectCallee() ? tagOf(call->getDirectCallee()->getReturnType(), ctx) : std::string{};
  if (const auto *bo = llvm::dyn_cast<BinaryOperator>(e)) {
    // Pointer arithmetic and assignment keep the tag of the pointer operand.
    if (bo->getLHS()->getType()->isPointerType())
      return exprTag(bo->getLHS(), ctx);
    if (bo->getRHS()->getType()->isPointerType())
      return exprTag(bo->getRHS(), ctx);
    return {};
  }
  if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e);
      uo && (uo->isIncrementDecrementOp() || uo->getOpcode() == UO_Plus))
    return exprTag(uo->getSubExpr(), ctx);
  if (const auto *co = llvm::dyn_cast<ConditionalOperator>(e))
    return exprTag(co->getTrueExpr(), ctx);
  return {};
}

bool isPtr(const Expr *e) { return e && e->getType()->isPointerType(); }

} // namespace

void NsPointerCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(unaryOperator(hasOperatorName("*")).bind("deref"), this);
  finder->addMatcher(arraySubscriptExpr().bind("sub"), this);
  finder->addMatcher(memberExpr(isArrow()).bind("arrow"), this);
  finder->addMatcher(binaryOperator(isAssignmentOperator()).bind("assign"), this);
  finder->addMatcher(callExpr().bind("call"), this);
  finder->addMatcher(explicitCastExpr().bind("cast"), this);
  finder->addMatcher(varDecl(hasInitializer(expr())).bind("var"), this);
  finder->addMatcher(returnStmt().bind("ret"), this);
}

void NsPointerCheck::check(const MatchFinder::MatchResult &result) {
  ASTContext &ctx = *result.Context;
  const SourceManager &sm = *result.SourceManager;

  auto enclosing = [&](const DynTypedNode &start) -> const FunctionDecl * {
    DynTypedNode cur = start;
    for (int i = 0; i < 64; ++i) {
      const auto parents = ctx.getParents(cur);
      if (parents.empty())
        return nullptr;
      cur = parents[0];
      if (const auto *fd = cur.get<FunctionDecl>())
        return fd;
    }
    return nullptr;
  };
  auto allowed = [&](const DynTypedNode &node, SourceLocation loc) {
    if (exempt(sm, loc))
      return true;
    for (const FunctionDecl *fd = enclosing(node); fd; fd = enclosing(DynTypedNode::create(*fd)))
      if (hasAnnotation(fd, "reloco_ns_translator") || inUnsafeTier(fd))
        return true;
    return false;
  };
  auto label = [](const std::string &t) { return t.empty() ? std::string("a normal") : "RELOCO_NS(" + t + ")"; };

  // Use of a foreign pointer's pointee.
  auto deref = [&](const Expr *ptr, const Expr *whole) {
    const std::string tag = exprTag(ptr, ctx);
    if (tag.empty() || allowed(DynTypedNode::create(*whole), whole->getBeginLoc()))
      return;
    diag(whole->getBeginLoc(), "dereference of RELOCO_NS(%0) pointer; the address is not valid in this address space") << tag;
    howToFix(whole->getBeginLoc(), "convert it first with a RELOCO_NS_TRANSLATOR function ('const T *p = translate(user_ptr, "
                                   "size);') or copy the data out through an accessor (copy_from_user style); never touch "
                                   "it directly");
  };
  // A value moving from one place to another must keep its address space.
  auto mismatch = [&](const std::string &to, const Expr *from, const Expr *whole, const char *what) {
    const std::string tag = exprTag(from, ctx);
    if (!isPtr(from) || tag == to || allowed(DynTypedNode::create(*whole), whole->getBeginLoc()))
      return;
    diag(whole->getBeginLoc(), "%0: %1 pointer used where %2 pointer is expected") << what << label(tag) << label(to);
    howToFix(whole->getBeginLoc(), "pointers of different address spaces never mix: translate through a RELOCO_NS_TRANSLATOR "
                                   "function, or declare the receiver with the same RELOCO_NS(tag)");
  };

  if (const auto *uo = result.Nodes.getNodeAs<UnaryOperator>("deref")) {
    deref(uo->getSubExpr(), uo);
  } else if (const auto *as = result.Nodes.getNodeAs<ArraySubscriptExpr>("sub")) {
    deref(isPtr(as->getBase()->IgnoreParenImpCasts()) ? as->getBase() : as->getIdx(), as);
  } else if (const auto *me = result.Nodes.getNodeAs<MemberExpr>("arrow")) {
    deref(me->getBase(), me);
  } else if (const auto *bo = result.Nodes.getNodeAs<BinaryOperator>("assign")) {
    if (isPtr(bo->getLHS()) && bo->getOpcode() == BO_Assign)
      mismatch(exprTag(bo->getLHS(), ctx), bo->getRHS(), bo, "assignment");
  } else if (const auto *call = result.Nodes.getNodeAs<CallExpr>("call")) {
    const FunctionDecl *fd = call->getDirectCallee();
    if (!fd || hasAnnotation(fd, "reloco_ns_translator"))
      return;
    const unsigned skip = llvm::isa<CXXOperatorCallExpr>(call) && llvm::isa<CXXMethodDecl>(fd) ? 1 : 0;
    for (unsigned i = skip; i < call->getNumArgs() && i - skip < fd->getNumParams(); ++i)
      if (isPtr(call->getArg(i)) || fd->getParamDecl(i - skip)->getType()->isPointerType())
        mismatch(tagOf(fd->getParamDecl(i - skip)->getType(), ctx), call->getArg(i), call->getArg(i), "argument");
  } else if (const auto *ce = result.Nodes.getNodeAs<ExplicitCastExpr>("cast")) {
    const Expr *sub = ce->getSubExprAsWritten();
    // Integers become foreign pointers freely (that is how they are made); pointers never change address space.
    if (isPtr(sub) && ce->getTypeAsWritten()->isPointerType())
      mismatch(tagOf(ce->getTypeAsWritten(), ctx), sub, ce, "cast");
  } else if (const auto *vd = result.Nodes.getNodeAs<VarDecl>("var")) {
    if (vd->getType()->isPointerType() && vd->getInit() && isPtr(vd->getInit()))
      mismatch(tagOf(vd->getType(), ctx), vd->getInit(), vd->getInit(), "initialisation");
  } else if (const auto *rs = result.Nodes.getNodeAs<ReturnStmt>("ret")) {
    const FunctionDecl *fd = enclosing(DynTypedNode::create(*rs));
    if (fd && rs->getRetValue() && fd->getReturnType()->isPointerType() && isPtr(rs->getRetValue()))
      mismatch(tagOf(fd->getReturnType(), ctx), rs->getRetValue(), rs->getRetValue(), "return");
  }
}

} // namespace reloco::tidy
