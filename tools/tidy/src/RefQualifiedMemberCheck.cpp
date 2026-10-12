// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang/Lex/Lexer.h"


namespace reloco::tidy {

namespace {

bool isInstantiation(const CXXMethodDecl *md) {
  if (md->isTemplateInstantiation() || md->getInstantiatedFromMemberFunction())
    return true;
  const CXXRecordDecl *rd = md->getParent();
  return rd->getTemplateSpecializationKind() == TSK_ImplicitInstantiation || rd->isLambda();
}

bool hasRvalueOverload(const CXXMethodDecl *md) {
  for (const Decl *d : md->getParent()->decls()) {
    // Member templates are wrapped in a FunctionTemplateDecl and are not part of methods().
    if (const auto *ft = llvm::dyn_cast<FunctionTemplateDecl>(d))
      d = ft->getTemplatedDecl();
    const auto *other = llvm::dyn_cast<CXXMethodDecl>(d);
    // A non-const '&&' overload (moving out of the temporary) is as good as a deleted 'const &&' one.
    if (other && other->getDeclName() == md->getDeclName() && other->getRefQualifier() == RQ_RValue)
      return true;
  }
  return false;
}

} // namespace

void RefQualifiedMemberCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxMethodDecl(unless(isImplicit()), unless(isStaticStorageClass()), unless(cxxConstructorDecl()),
                                   unless(cxxDestructorDecl()), unless(isDeleted()))
                         .bind("method"),
                     this);
}

void RefQualifiedMemberCheck::check(const MatchFinder::MatchResult &result) {
  const auto *md = result.Nodes.getNodeAs<CXXMethodDecl>("method");
  if (!md || md->getCanonicalDecl() != md || isInstantiation(md) || md->isVirtual() || md->size_overridden_methods() ||
      md->isCopyAssignmentOperator() || md->isMoveAssignmentOperator() || exempt(*result.SourceManager, md->getLocation()))
    return;

  // The '&' must be added to the declaration and to every out-of-line definition, or they stop matching.
  const auto addQualifierFixes = [&](DiagnosticBuilder &d, const CXXMethodDecl *method) {
    for (const FunctionDecl *redecl : method->redecls())
      if (const auto *m = llvm::dyn_cast<CXXMethodDecl>(redecl))
        if (const SourceLocation at =
                functionQualifierEnd(m, *result.SourceManager, result.Context->getLangOpts(), false);
            at.isValid())
          d << FixItHint::CreateInsertion(at, " &");
  };

  const CXXRecordDecl *parent = md->getParent();
  const bool aliasing = isAliasing(md->getReturnType());
  const bool handle = recordIsPointer(parent);
  // A call operator usually returns an alias of its argument (key extractors, projections), not of the functor.
  if (md->getOverloadedOperator() == OO_Call)
    return;
  // Dereferencing a temporary inside one full expression ('*make()', 'make()->f()') is the idiom the
  // operator exists for, so unary '*' and '->' keep binding temporaries.
  if (md->getRefQualifier() == RQ_LValue && md->isConst() &&
      (md->getOverloadedOperator() == OO_Arrow || (md->getOverloadedOperator() == OO_Star && md->param_size() == 0)))
    return;

  // By-value handles and proxies (RELOCO_POINTER) are themselves the temporary: `region[i] = x` must work.
  if (handle)
    return;

  if (md->isConst()) {
    // Non-aliasing const getters return values, which stay valid on temporaries.
    if (!aliasing)
      return;
    if (md->getRefQualifier() == RQ_None) {
      auto d = diag(md->getLocation(), "const member %0 returns an alias of its object but is not ref-qualified; "
                                       "declare it 'const &' and delete the 'const &&' overload")
               << md;
      addQualifierFixes(d, md);
    }
    else if (md->getRefQualifier() == RQ_LValue && !hasRvalueOverload(md)) {
      auto d = diag(md->getLocation(), "const & member %0 returns an alias of its object and still binds temporaries; "
                                       "add a deleted 'const &&' overload (RELOCO_BLOCK_RVALUE_ACCESS)")
               << md;
      // Copy the declaration up to and including the '&' and turn it into 'const &&' = delete.
      const SourceManager &sm = *result.SourceManager;
      const LangOptions &lo = result.Context->getLangOpts();
      const SourceLocation refEnd = functionQualifierEnd(md, sm, lo, true);
      const SourceLocation begin = md->getInnerLocStart();
      const SourceLocation bodyEnd = md->hasBody() ? md->getEndLoc() : SourceLocation();
      if (refEnd.isValid() && begin.isValid() && bodyEnd.isValid() && !begin.isMacroID() && !bodyEnd.isMacroID() &&
          !md->getDescribedFunctionTemplate() && sm.isWrittenInSameFile(begin, refEnd)) {
        const std::string head = Lexer::getSourceText(CharSourceRange::getCharRange(begin, refEnd), sm, lo).str();
        d << FixItHint::CreateInsertion(Lexer::getLocForEndOfToken(bodyEnd, 0, sm, lo),
                                        "\n\n  " + head + "& = delete;");
      }
    }
    return;
  }

  // A non-const member that returns no alias cannot leave a reference into a dying temporary behind.
  if (!aliasing && !strict_)
    return;
  if (md->getRefQualifier() == RQ_None) {
    auto d = diag(md->getLocation(), "non-const member %0 is callable on temporaries; qualify it with '&'") << md;
    addQualifierFixes(d, md);
  }
}

} // namespace reloco::tidy
