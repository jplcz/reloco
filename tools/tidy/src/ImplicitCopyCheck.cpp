// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ParentMapContext.h"

namespace reloco::tidy {

namespace {

bool hasCopyableMarker(const CXXRecordDecl *rd) {
  const auto marked = [](const Decl *d) {
    for (const auto *a : d->specific_attrs<AnnotateAttr>())
      if (a->getAnnotation() == "reloco_copyable")
        return true;
    return false;
  };
  if (marked(rd))
    return true;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
    return marked(spec->getSpecializedTemplate()->getTemplatedDecl());
  return false;
}


void collectTypeArgs(const TemplateArgumentList *args, llvm::SmallVectorImpl<QualType> &out) {
  if (!args)
    return;
  for (const TemplateArgument &a : args->asArray()) {
    if (a.getKind() == TemplateArgument::Type)
      out.push_back(a.getAsType().getCanonicalType());
    else if (a.getKind() == TemplateArgument::Pack)
      for (const TemplateArgument &p : a.pack_elements())
        if (p.getKind() == TemplateArgument::Type)
          out.push_back(p.getAsType().getCanonicalType());
  }
}

bool mentionsAny(QualType t, llvm::ArrayRef<QualType> args, int depth = 0) {
  t = t.getNonReferenceType().getCanonicalType();
  for (const QualType &a : args)
    if (a == t)
      return true;
  if (depth > 3)
    return false;
  if (const auto *spec = llvm::dyn_cast_or_null<ClassTemplateSpecializationDecl>(t->getAsCXXRecordDecl())) {
    llvm::SmallVector<QualType, 4> inner;
    collectTypeArgs(&spec->getTemplateArgs(), inner);
    for (const QualType &i : inner)
      if (mentionsAny(i, args, depth + 1))
        return true;
  }
  return false;
}

// A copy inside an instantiated template of a type built from the instantiation's own arguments is the caller's
// choice of T (generic code cannot know whether T is cheap or move-only), not a defect of the template itself.
bool dependsOnInstantiationArgs(QualType copied, const FunctionDecl *fn) {
  llvm::SmallVector<QualType, 8> args;
  if (fn) {
    collectTypeArgs(fn->getTemplateSpecializationArgs(), args);
    if (const auto *m = llvm::dyn_cast<CXXMethodDecl>(fn))
      if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(m->getParent()))
        collectTypeArgs(&spec->getTemplateArgs(), args);
  }
  return !args.empty() && mentionsAny(copied, args);
}

const FunctionDecl *enclosingFn(const DynTypedNode &n, ASTContext &ctx) {
  DynTypedNode cur = n;
  for (int i = 0; i < 64; ++i) {
    auto parents = ctx.getParents(cur);
    if (parents.empty())
      return nullptr;
    cur = parents[0];
    if (const auto *fd = cur.get<FunctionDecl>())
      return fd;
  }
  return nullptr;
}

bool isWrapper(const Stmt *s) {
  return llvm::isa<ExprWithCleanups>(s) || llvm::isa<CXXBindTemporaryExpr>(s) ||
         llvm::isa<MaterializeTemporaryExpr>(s) || llvm::isa<ImplicitCastExpr>(s) || llvm::isa<ConstantExpr>(s);
}

} // namespace

void ImplicitCopyCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxConstructExpr(hasDeclaration(cxxConstructorDecl(isCopyConstructor())),
                                      unless(hasAncestor(functionDecl(isImplicit()))))
                         .bind("ctor"),
                     this);
  finder->addMatcher(cxxOperatorCallExpr(isAssignmentOperator(),
                                         callee(cxxMethodDecl(isCopyAssignmentOperator())),
                                         unless(hasAncestor(functionDecl(isImplicit()))))
                         .bind("assign"),
                     this);
}

void ImplicitCopyCheck::check(const MatchFinder::MatchResult &result) {
  const SourceManager &sm = *result.SourceManager;
  const CXXRecordDecl *rd = nullptr;
  SourceLocation loc;

  if (const auto *ce = result.Nodes.getNodeAs<CXXConstructExpr>("ctor")) {
    rd = ce->getConstructor()->getParent();
    loc = ce->getBeginLoc();
    if (ce->isElidable() || ce->isListInitialization())
      return;
    // Climb to the syntactic context to tell `T y = x` (implicit) from `T y(x)` / `T(x)` (explicit clone).
    DynTypedNode cur = DynTypedNode::create(*ce);
    for (;;) {
      auto parents = result.Context->getParents(cur);
      if (parents.empty())
        break;
      cur = parents[0];
      if (const auto *st = cur.get<Stmt>(); st && isWrapper(st))
        continue;
      break;
    }
    // A member/base initialiser (`: m(o)`) hangs directly off the constructor decl: a direct-init, i.e. explicit.
    if (cur.get<CXXConstructExpr>() || cur.get<CXXConstructorDecl>())
      return;
    if (const auto *vd = cur.get<VarDecl>(); vd && vd->getInitStyle() != VarDecl::CInit)
      return;
    if (cur.get<CXXFunctionalCastExpr>() || cur.get<CXXStaticCastExpr>() || cur.get<CXXCtorInitializer>())
      return;
  } else if (const auto *oc = result.Nodes.getNodeAs<CXXOperatorCallExpr>("assign")) {
    // Assigning from a temporary is a copy only because the type has no move assignment; there is nothing to move.
    if (oc->getNumArgs() == 2 && !oc->getArg(1)->IgnoreImplicit()->isLValue())
      return;
    rd = llvm::cast<CXXMethodDecl>(oc->getDirectCallee())->getParent();
    loc = oc->getOperatorLoc();
    if (rd && dependsOnInstantiationArgs(oc->getArg(0)->getType(), enclosingFn(DynTypedNode::create(*oc), *result.Context)))
      return;
  }
  if (const auto *ce = result.Nodes.getNodeAs<CXXConstructExpr>("ctor");
      ce && rd && dependsOnInstantiationArgs(ce->getType(), enclosingFn(DynTypedNode::create(*ce), *result.Context)))
    return;

  if (!rd || rd->isLambda() || exempt(sm, loc) || sm.isInSystemHeader(rd->getLocation()) ||
      hasCopyableMarker(rd) || recordIsPointer(rd))
    return;
  diag(loc, "implicit copy of '%0'; move it, copy explicitly with '%0(x)', or mark the type RELOCO_COPYABLE")
      << rd->getName();
  howToFix(loc, "move it ('take(std::move(x))' or return by value), clone on purpose ('T copy(x);'), or, for small "
                "plain-data types, declare 'struct RELOCO_COPYABLE T { ... };'");
}

} // namespace reloco::tidy
