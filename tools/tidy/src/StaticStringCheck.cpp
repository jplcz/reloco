// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

namespace reloco::tidy {

namespace {

bool isStatic(const Expr *e, const ASTContext &ctx) {
  e = e->IgnoreParenImpCasts();
  if (llvm::isa<StringLiteral>(e) || llvm::isa<CXXNullPtrLiteralExpr>(e) || llvm::isa<GNUNullExpr>(e))
    return true;
  if (llvm::isa<SourceLocExpr>(e) || llvm::isa<ImplicitValueInitExpr>(e) || llvm::isa<CXXDefaultInitExpr>(e))
    return true;
  if (const auto *il = llvm::dyn_cast<InitListExpr>(e); il && il->getNumInits() <= 1)
    return il->getNumInits() == 0 || isStatic(il->getInit(0), ctx);
  if (e->isNullPointerConstant(const_cast<ASTContext &>(ctx), Expr::NPC_ValueDependentIsNull))
    return true;
  if (const auto *co = llvm::dyn_cast<ConditionalOperator>(e))
    return isStatic(co->getTrueExpr(), ctx) && isStatic(co->getFalseExpr(), ctx);
  if (const auto *bo = llvm::dyn_cast<BinaryOperator>(e); bo && (bo->getOpcode() == BO_Add || bo->getOpcode() == BO_Sub))
    return isStatic(bo->getLHS(), ctx);
  if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e); uo && uo->getOpcode() == UO_AddrOf)
    if (const auto *as = llvm::dyn_cast<ArraySubscriptExpr>(uo->getSubExpr()->IgnoreParenImpCasts()))
      return isStatic(as->getBase(), ctx);
  if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(e)) {
    if (const auto *vd = llvm::dyn_cast<VarDecl>(dre->getDecl()))
      return (vd->hasGlobalStorage() && !vd->getType()->isPointerType()) || isStaticStrType(vd->getType(), ctx);
    return false;
  }
  if (const auto *me = llvm::dyn_cast<MemberExpr>(e))
    return isStaticStrType(me->getMemberDecl()->getType(), ctx);
  if (const auto *call = llvm::dyn_cast<CallExpr>(e))
    if (const FunctionDecl *fd = call->getDirectCallee())
      return isStaticStrType(fd->getReturnType(), ctx);
  return false;
}

} // namespace

void StaticStringCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(returnStmt(hasReturnValue(expr().bind("value")), forFunction(functionDecl().bind("fn"))).bind("ret"), this);
  finder->addMatcher(callExpr(unless(cxxOperatorCallExpr(hasOverloadedOperatorName("=")))).bind("call"), this);
  finder->addMatcher(cxxConstructExpr().bind("construct"), this);
  finder->addMatcher(varDecl(hasInitializer(expr().bind("value"))).bind("var"), this);
  finder->addMatcher(fieldDecl(hasInClassInitializer(expr().bind("value"))).bind("field"), this);
  finder->addMatcher(cxxCtorInitializer(forField(fieldDecl().bind("field")), withInitializer(expr().bind("value"))).bind("init"), this);
  finder->addMatcher(binaryOperator(hasOperatorName("=")).bind("assign"), this);
}

void StaticStringCheck::check(const MatchFinder::MatchResult &result) {
  const ASTContext &ctx = *result.Context;
  const SourceManager &sm = *result.SourceManager;

  auto verify = [&](const Expr *value, QualType target, const Decl *anchor) {
    if (!value || value->isTypeDependent() || !isStaticStrType(target, ctx) || isStatic(value, ctx))
      return;
    if (exempt(sm, value->getBeginLoc()) || (anchor && inUnsafeTier(anchor)))
      return;
    diag(value->getBeginLoc(), "value is not known to have static lifetime but is used as a RELOCO_STATIC_STR string; "
                               "pass a string literal or another RELOCO_STATIC_STR value");
    howToFix(value->getBeginLoc(), "a pointer into a std::string/array/buffer can dangle: take 'reloco::string_view' instead, "
                                   "or copy to storage that lives for the whole program");
  };

  if (const auto *ret = result.Nodes.getNodeAs<ReturnStmt>("ret")) {
    const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
    if (!fn->isTemplateInstantiation())
      verify(ret->getRetValue(), fn->getReturnType(), fn);
  } else if (const auto *call = result.Nodes.getNodeAs<CallExpr>("call")) {
    const FunctionDecl *fd = call->getDirectCallee();
    if (!fd)
      return;
    const unsigned skip = llvm::isa<CXXOperatorCallExpr>(call) && llvm::isa<CXXMethodDecl>(fd) ? 1 : 0;
    for (unsigned i = skip; i < call->getNumArgs() && i - skip < fd->getNumParams(); ++i)
      verify(call->getArg(i), fd->getParamDecl(i - skip)->getType(), nullptr);
  } else if (const auto *cc = result.Nodes.getNodeAs<CXXConstructExpr>("construct")) {
    const CXXConstructorDecl *ctor = cc->getConstructor();
    for (unsigned i = 0; i < cc->getNumArgs() && i < ctor->getNumParams(); ++i)
      verify(cc->getArg(i), ctor->getParamDecl(i)->getType(), nullptr);
  } else if (const auto *vd = result.Nodes.getNodeAs<VarDecl>("var")) {
    verify(result.Nodes.getNodeAs<Expr>("value"), vd->getType(), vd);
  } else if (const auto *fd = result.Nodes.getNodeAs<FieldDecl>("field")) {
    verify(result.Nodes.getNodeAs<Expr>("value"), fd->getType(), fd);
  } else if (const auto *bo = result.Nodes.getNodeAs<BinaryOperator>("assign")) {
    const Expr *lhs = bo->getLHS()->IgnoreParenImpCasts();
    QualType t;
    if (const auto *dre = llvm::dyn_cast<DeclRefExpr>(lhs))
      t = dre->getDecl()->getType();
    else if (const auto *me = llvm::dyn_cast<MemberExpr>(lhs))
      t = me->getMemberDecl()->getType();
    if (!t.isNull())
      verify(bo->getRHS(), t, nullptr);
  }
}

} // namespace reloco::tidy
