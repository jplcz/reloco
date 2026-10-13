// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Shared description of what counts as a lock for reloco-lock-alias and reloco-lock-balance.

#pragma once

#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"

namespace reloco::tidy::lockmodel {

using namespace clang;

inline bool annotated(const Decl *d, llvm::StringRef name) {
  if (!d)
    return false;
  for (const Decl *r : d->redecls())
    for (const auto *a : r->specific_attrs<AnnotateAttr>())
      if (a->getAnnotation() == name)
        return true;
  return false;
}

// The class definition behind a type, looking through references, pointers and arrays; the template pattern for
// specialisations is exposed through `pattern()`.
inline const CXXRecordDecl *recordOfType(QualType t) {
  while (!t.isNull()) {
    t = t.getNonReferenceType();
    if (t->isPointerType())
      t = t->getPointeeType();
    else if (t->isArrayType())
      t = llvm::cast<ArrayType>(t.getCanonicalType().getTypePtr())->getElementType();
    else
      break;
  }
  if (t.isNull() || t->isDependentType())
    return nullptr;
  return t.getCanonicalType()->getAsCXXRecordDecl();
}

inline const CXXRecordDecl *pattern(const CXXRecordDecl *rd) {
  if (const auto *spec = llvm::dyn_cast_or_null<ClassTemplateSpecializationDecl>(rd))
    return spec->getSpecializedTemplate()->getTemplatedDecl();
  return rd;
}

// A capability attribute on any redeclaration, so a shim that forward-declares a legacy type still counts.
inline bool hasCapability(const CXXRecordDecl *rd) {
  if (!rd)
    return false;
  for (const Decl *r : rd->redecls())
    if (r->hasAttr<CapabilityAttr>())
      return true;
  return pattern(rd)->hasAttr<CapabilityAttr>();
}

// Parses a "name:N" function annotation (RELOCO_ACQUIRES(N) and friends) into N.
inline bool fnArgAnnotation(const FunctionDecl *fd, llvm::StringRef name, unsigned &index) {
  if (!fd)
    return false;
  for (const Decl *r : fd->redecls())
    for (const auto *a : r->specific_attrs<AnnotateAttr>()) {
      llvm::StringRef text = a->getAnnotation();
      if (text.consume_front(name) && text.consume_front(":") && !text.getAsInteger(10, index))
        return true;
    }
  return false;
}

inline bool ownAnnotation(const CXXRecordDecl *rd, llvm::StringRef annotation) {
  return annotated(rd, annotation) || annotated(pattern(rd), annotation);
}

inline bool exposesLockApi(const CXXRecordDecl *rd) {
  for (const CXXMethodDecl *m : rd->methods())
    if (!m->isStatic() && m->getIdentifier() &&
        (m->getName() == "lock" || m->getName() == "lock_shared" || m->getName() == "read_lock" ||
         m->getName() == "write_lock"))
      return true;
  return false;
}

// True when `direct` holds for `rd`, for one of its base classes, or for a by-value member of a class that exposes its
// own lock()-style API. That covers locks extended by derivation (`struct my_lock : ticket_spin_lock`) and by
// composition (`class my_lock { ticket_spin_lock m_lock; public: void lock(); }`), so annotating the innermost lock
// is enough. Reference and pointer members are ignored: those are guards borrowing a lock, not wrappers owning one.
template <typename Pred> bool lockLike(const CXXRecordDecl *rd, const Pred &direct, int depth = 0) {
  if (!rd || depth > 8)
    return false;
  if (direct(rd))
    return true;
  if (!rd->hasDefinition())
    return false;
  for (const CXXBaseSpecifier &base : rd->bases())
    if (const CXXRecordDecl *b = base.getType()->isDependentType() ? nullptr : base.getType()->getAsCXXRecordDecl();
        lockLike(b, direct, depth + 1))
      return true;
  if (!exposesLockApi(rd))
    return false;
  for (const FieldDecl *f : rd->fields()) {
    QualType t = f->getType();
    while (t->isArrayType())
      t = llvm::cast<ArrayType>(t.getCanonicalType().getTypePtr())->getElementType();
    if (t->isReferenceType() || t->isPointerType() || t->isDependentType())
      continue;
    if (lockLike(t.getCanonicalType()->getAsCXXRecordDecl(), direct, depth + 1))
      return true;
  }
  return false;
}

// Annotation on the class, its template pattern, or a base class.
inline bool classHas(const CXXRecordDecl *rd, llvm::StringRef annotation) {
  if (ownAnnotation(rd, annotation))
    return true;
  if (!rd || !rd->hasDefinition())
    return false;
  for (const CXXBaseSpecifier &base : rd->bases())
    if (!base.getType()->isDependentType() && classHas(base.getType()->getAsCXXRecordDecl(), annotation))
      return true;
  return false;
}

// A spin-style lock that makes guards over it atomic scopes: RELOCO_ATOMIC_LOCK, inherited or wrapped.
inline bool isAtomicLockRecord(const CXXRecordDecl *rd) {
  return lockLike(rd, [](const CXXRecordDecl *r) { return ownAnnotation(r, "reloco_atomic_lock"); });
}

// A lock: a Clang capability (RELOCO_CAPABILITY) or a RELOCO_ATOMIC_LOCK class, inherited or wrapped.
inline bool isLockRecord(const CXXRecordDecl *rd) {
  return lockLike(rd, [](const CXXRecordDecl *r) {
    return hasCapability(r) || ownAnnotation(r, "reloco_atomic_lock");
  });
}

// A legacy lock type whose pointers are accepted (RELOCO_LEGACY_LOCK, possibly applied by a shim header).
inline bool isLegacyLock(const CXXRecordDecl *rd) { return classHas(rd, "reloco_legacy_lock"); }

inline bool isLockType(QualType t) { return isLockRecord(recordOfType(t)); }

// Classes that implement locking themselves: locks, RAII guards (scoped capabilities, atomic scopes/guards).
inline bool isLockImplementation(const CXXRecordDecl *rd) {
  if (!rd)
    return false;
  return isLockRecord(rd) || rd->hasAttr<ScopedLockableAttr>() || pattern(rd)->hasAttr<ScopedLockableAttr>() ||
         classHas(rd, "reloco_atomic_scope") || classHas(rd, "reloco_atomic_guard") || classHas(rd, "reloco_lock_impl");
}

// True when `d` sits inside a lock implementation class or is a function marked RELOCO_LOCK_IMPL.
inline bool inLockImplementation(const Decl *d) {
  for (const DeclContext *dc = d ? d->getDeclContext() : nullptr; dc; dc = dc->getParent()) {
    if (const auto *rd = llvm::dyn_cast<CXXRecordDecl>(dc); rd && isLockImplementation(rd))
      return true;
    if (const auto *fd = llvm::dyn_cast<FunctionDecl>(dc);
        fd && (annotated(fd, "reloco_lock_impl") || annotated(fd->getPrimaryTemplate()
                                                                      ? fd->getPrimaryTemplate()->getTemplatedDecl()
                                                                      : nullptr,
                                                                  "reloco_lock_impl")))
      return true;
  }
  return false;
}

} // namespace reloco::tidy::lockmodel
