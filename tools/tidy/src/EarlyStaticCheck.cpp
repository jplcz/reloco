// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-early-static: RELOCO_EARLY_STATIC types and variables must need no runtime construction or destruction, so
// they are usable before the C++ runtime initialises (bare-metal startup).

#include "Checks.h"

#include "clang/AST/Attr.h"
#include "clang/Lex/Lexer.h"

namespace reloco::tidy {

namespace {

bool hasAnnotation(const Decl *d, llvm::StringRef name) {
  for (const Decl *r : d->redecls())
    for (const auto *a : r->specific_attrs<AnnotateAttr>())
      if (a->getAnnotation() == name)
        return true;
  return false;
}

bool markedRecord(const CXXRecordDecl *rd) {
  if (hasAnnotation(rd, "reloco_early_static"))
    return true;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
    return hasAnnotation(spec->getSpecializedTemplate()->getTemplatedDecl(), "reloco_early_static");
  return false;
}

const CXXRecordDecl *recordOf(QualType t) {
  while (const auto *at = t->getAsArrayTypeUnsafe())
    t = at->getElementType();
  return t->getAsCXXRecordDecl();
}

} // namespace

void EarlyStaticCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit()), unless(isTemplateInstantiation())).bind("class"), this);
  finder->addMatcher(varDecl(hasStaticStorageDuration(), unless(isImplicit()), unless(parmVarDecl())).bind("var"), this);
}

void EarlyStaticCheck::check(const MatchFinder::MatchResult &result) {
  ASTContext &ctx = *result.Context;
  const SourceManager &sm = *result.SourceManager;

  if (const auto *rd = result.Nodes.getNodeAs<CXXRecordDecl>("class")) {
    if (!markedRecord(rd) || rd->isInvalidDecl() || exempt(sm, rd->getLocation()))
      return;
    if (rd->isDependentType())
      return;
    // A specific reason (constructor, destructor) is reported instead of the generic "not a literal type".
    bool specific = false;
    if (!rd->hasTrivialDestructor()) {
      specific = true;
      const CXXDestructorDecl *dtor = rd->getDestructor();
      diag(dtor && !dtor->isImplicit() ? dtor->getLocation() : rd->getLocation(),
           "RELOCO_EARLY_STATIC type %0 has a non-trivial destructor; destroying a static object registers an atexit handler "
           "at runtime")
          << rd;
      howToFix(rd->getLocation(), "make the destructor trivial ('~T() = default;' with trivially destructible members) or "
                                  "drop RELOCO_EARLY_STATIC and construct the object after startup");
    }
    for (const CXXConstructorDecl *c : rd->ctors()) {
      if (c->isImplicit() || c->isDeleted() || c->isConstexpr() || c->isDefaulted() || c->isCopyOrMoveConstructor())
        continue;
      specific = true;
      diag(c->getLocation(), "constructor of RELOCO_EARLY_STATIC type %0 is not constexpr") << rd;
      howToFix(c->getLocation(), "declare it 'constexpr' and initialise every member in the member-init list, so no code "
                                 "runs before main");
    }
    if (!specific && !rd->isLiteral()) {
      diag(rd->getLocation(), "RELOCO_EARLY_STATIC type %0 is not a literal type; it cannot be constant-initialised") << rd;
      howToFix(rd->getLocation(), "give it a constexpr constructor, no virtual bases and only literal members, so a "
                                  "'constinit' object of it is emitted into .data without startup code");
    }
    return;
  }

  const auto *vd = result.Nodes.getNodeAs<VarDecl>("var");
  if (!vd || vd->isInvalidDecl() || vd->isStaticDataMember() == false && vd->isLocalVarDecl() == false && vd->isFileVarDecl() == false)
    return;
  if (vd->getType()->isDependentType() || vd->isThisDeclarationADefinition() == VarDecl::DeclarationOnly || vd->isExternC())
    return;
  const CXXRecordDecl *rd = recordOf(vd->getType());
  const bool required = hasAnnotation(vd, "reloco_early_static") || (rd && markedRecord(rd));
  if (!required || exempt(sm, vd->getLocation()) || !vd->hasInit() && !(rd && rd->hasDefinition()))
    return;

  if (vd->isConstexpr() || vd->hasAttr<ConstInitAttr>())
    return;
  const Expr *init = vd->getInit();
  if (init && !init->isValueDependent() && init->isConstantInitializer(ctx, vd->getType()->isReferenceType()))
    return;
  if (vd->hasConstantInitialization())
    return;

  auto d = diag(vd->getLocation(), "RELOCO_EARLY_STATIC object %0 is initialised at runtime; it must be constant-initialised")
           << vd;
  diag(vd->getLocation(), "dynamic initialisation runs a constructor before main and its order across translation units "
                          "is unspecified", DiagnosticIDs::Note);
  howToFix(vd->getLocation(), "declare it 'constinit' (C++20) or 'constexpr' and give the type a constexpr constructor; "
                              "then the compiler rejects any runtime initialisation");
  if (getLangOpts().CPlusPlus20 && !vd->getBeginLoc().isMacroID() && vd->getBeginLoc().isValid())
    d << FixItHint::CreateInsertion(vd->getBeginLoc(), "constinit ");
}

} // namespace reloco::tidy
