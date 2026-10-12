// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// reloco-send-sync: arguments handed to RELOCO_REQUIRE_SEND / RELOCO_REQUIRE_SYNC parameters (thread and task spawn
// points) must be Send / Sync, decided structurally in the manner of Rust's auto traits.

#include "Checks.h"

#include "clang/AST/Attr.h"

#include <set>
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

// Marker on the class or, for template specialisations, on the primary template.
bool marked(const CXXRecordDecl *rd, llvm::StringRef name) {
  if (hasAnnotation(rd, name))
    return true;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
    return hasAnnotation(spec->getSpecializedTemplate()->getTemplatedDecl(), name);
  return false;
}

bool isCapabilityRecord(const CXXRecordDecl *rd) {
  if (rd->hasAttr<CapabilityAttr>())
    return true;
  if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
    return spec->getSpecializedTemplate()->getTemplatedDecl()->hasAttr<CapabilityAttr>();
  return false;
}

bool isAtomicRecord(const CXXRecordDecl *rd) { return rd->getIdentifier() && rd->getName().contains("atomic"); }

class Traits {
public:
  explicit Traits(const ASTContext &ctx) : ctx_(ctx) {}

  bool send(QualType t, std::string &why) { return send(t, why, 0); }
  bool sync(QualType t, std::string &why) { return sync(t, why, 0); }

private:
  bool send(QualType t, std::string &why, int depth) {
    t = t.getCanonicalType().getUnqualifiedType();
    if (depth > 6 || t.isNull() || t->isDependentType())
      return true;
    if (t->isPointerType() || t->isMemberPointerType()) {
      why = "contains a raw pointer";
      return false;
    }
    if (t->isReferenceType()) {
      why = "contains a reference";
      return false;
    }
    if (const auto *at = ctx_.getAsConstantArrayType(t))
      return send(at->getElementType(), why, depth + 1);
    const CXXRecordDecl *rd = t->getAsCXXRecordDecl();
    if (!rd || !rd->hasDefinition())
      return true;
    if (marked(rd, "reloco_not_send")) {
      why = "class '" + rd->getNameAsString() + "' is RELOCO_NOT_SEND";
      return false;
    }
    if (marked(rd, "reloco_send") || isAtomicRecord(rd) || isCapabilityRecord(rd))
      return true;
    if (!visiting_.insert(rd).second)
      return true;
    struct Pop {
      std::set<const CXXRecordDecl *> &s;
      const CXXRecordDecl *r;
      ~Pop() { s.erase(r); }
    } pop{visiting_, rd};

    if (rd->isLambda()) {
      for (const LambdaCapture &c : rd->captures()) {
        if (c.capturesThis() || c.getCaptureKind() == LCK_ByRef) {
          why = "lambda captures by reference or 'this'; capture by value (init-capture a moved value)";
          return false;
        }
      }
    }
    // Owners (containers, smart pointers) hold their storage through pointers; they are Send iff their elements are.
    if (rd->hasAttr<OwnerAttr>() || hasAnnotation(rd, "reloco_owner")) {
      if (const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(rd))
        for (const TemplateArgument &a : spec->getTemplateArgs().asArray())
          if (a.getKind() == TemplateArgument::Type && !send(a.getAsType(), why, depth + 1))
            return false;
      return true;
    }
    if (rd->hasAttr<PointerAttr>()) {
      why = "class '" + rd->getNameAsString() + "' is a non-owning view (RELOCO_POINTER)";
      return false;
    }
    for (const CXXBaseSpecifier &b : rd->bases())
      if (!send(b.getType(), why, depth + 1))
        return false;
    for (const FieldDecl *f : rd->fields())
      if (!send(f->getType(), why, depth + 1)) {
        why = "member '" + f->getNameAsString() + "' " + why;
        return false;
      }
    return true;
  }

  bool sync(QualType t, std::string &why, int depth) {
    t = t.getNonReferenceType().getCanonicalType();
    if (t->isDependentType() || depth > 6)
      return true;
    if (t->isPointerType())
      return sync(t->getPointeeType(), why, depth + 1);
    if (t->isBuiltinType() || t->isEnumeralType())
      return t.isConstQualified() || (why = "mutable non-atomic scalar shared between threads", false);
    const CXXRecordDecl *rd = t->getAsCXXRecordDecl();
    if (!rd || !rd->hasDefinition())
      return true;
    if (marked(rd, "reloco_sync") || isAtomicRecord(rd) || isCapabilityRecord(rd))
      return true;
    if (t.isConstQualified() && !hasMutableState(rd))
      return true;
    // A mutex owner whose other members are guarded (enforced by reloco-lock-guard) is Sync.
    bool hasMutex = false;
    for (const FieldDecl *f : rd->fields())
      if (const CXXRecordDecl *fr = f->getType()->getAsCXXRecordDecl(); fr && isCapabilityRecord(fr))
        hasMutex = true;
    if (hasMutex)
      return true;
    why = "class '" + rd->getNameAsString() + "' has unsynchronised mutable state; mark it RELOCO_SYNC, guard it with a "
          "mutex, or make it immutable";
    return false;
  }

  bool hasMutableState(const CXXRecordDecl *rd) const {
    for (const FieldDecl *f : rd->fields())
      if (f->isMutable())
        return true;
    return false;
  }

  const ASTContext &ctx_;
  std::set<const CXXRecordDecl *> visiting_;
};

} // namespace

void SendSyncCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(callExpr(unless(isInTemplateInstantiation()), callee(functionDecl())).bind("call"), this);
}

void SendSyncCheck::check(const MatchFinder::MatchResult &result) {
  const auto *call = result.Nodes.getNodeAs<CallExpr>("call");
  const FunctionDecl *fd = call->getDirectCallee();
  if (!fd || exempt(*result.SourceManager, call->getBeginLoc()))
    return;
  // Template instantiations lose parameter attributes only on the pattern; consult the primary when present.
  const FunctionDecl *pattern = fd->getPrimaryTemplate() ? fd->getPrimaryTemplate()->getTemplatedDecl() : fd;
  const bool shift = llvm::isa<CXXOperatorCallExpr>(call) && llvm::isa<CXXMethodDecl>(fd);

  Traits traits(*result.Context);
  for (unsigned i = 0; i < call->getNumArgs(); ++i) {
    const unsigned pi = shift ? i - 1 : i;
    if (shift && i == 0)
      continue;
    if (pi >= pattern->getNumParams())
      break;
    const ParmVarDecl *p = pattern->getParamDecl(pi);
    const bool needSend = hasAnnotation(p, "reloco_require_send") || hasAnnotation(pattern, "reloco_require_send");
    const bool needSync = hasAnnotation(p, "reloco_require_sync");
    if (!needSend && !needSync)
      continue;
    const Expr *arg = call->getArg(i);
    QualType t = arg->IgnoreParenImpCasts()->getType();
    std::string why;
    if (needSend && !traits.send(t.getNonReferenceType(), why)) {
      diag(arg->getBeginLoc(), "argument of type %0 passed to a RELOCO_REQUIRE_SEND parameter is not Send: %1") << t << why;
      howToFix(arg->getBeginLoc(), "pass owned values (moved into the task) instead of pointers, references or views; "
                                   "capture lambdas by value; or mark the type RELOCO_SEND after auditing it");
    }
    why.clear();
    if (needSync && !traits.sync(t, why)) {
      diag(arg->getBeginLoc(), "argument of type %0 shared through a RELOCO_REQUIRE_SYNC parameter is not Sync: %1") << t << why;
      howToFix(arg->getBeginLoc(), "make the shared state atomic, put it behind a mutex with RELOCO_GUARDED_BY members, make "
                                   "it immutable, or mark the type RELOCO_SYNC after auditing it");
    }
  }
}

} // namespace reloco::tidy
