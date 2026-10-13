// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// Reads the named lifetimes written with RELOCO_LT / RELOCO_LT_RETURN / RELOCO_LT_THIS. Each macro is an
// `annotate("reloco_lt[_return|_this]:<name>")` attribute, so no attribute-argument API is involved.

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <algorithm>
#include <string>

namespace reloco::tidy {

using LifetimeSet = llvm::SmallVector<std::string, 2>;

inline LifetimeSet lifetimeNames(const clang::Decl *d, llvm::StringRef prefix) {
  LifetimeSet out;
  if (!d)
    return out;
  for (const auto *an : d->specific_attrs<clang::AnnotateAttr>())
    if (an->getAnnotation().starts_with(prefix))
      out.push_back(an->getAnnotation().drop_front(prefix.size()).str());
  return out;
}

// Parameters, fields and variables all carry their lifetime the same way.
inline LifetimeSet declLifetimes(const clang::ValueDecl *d) { return lifetimeNames(d, "reloco_lt:"); }
inline LifetimeSet paramLifetimes(const clang::ParmVarDecl *p) { return declLifetimes(p); }
inline LifetimeSet returnLifetimes(const clang::FunctionDecl *fd) { return lifetimeNames(fd, "reloco_lt_return:"); }
inline LifetimeSet thisLifetimes(const clang::FunctionDecl *fd) { return lifetimeNames(fd, "reloco_lt_this:"); }

inline bool hasStatic(const LifetimeSet &s) { return std::find(s.begin(), s.end(), "static") != s.end(); }

// `static` is not a region to borrow from: it only states a requirement, so it never ties a parameter to the result.
inline bool sharesLifetime(const LifetimeSet &a, const LifetimeSet &b) {
  return std::any_of(a.begin(), a.end(), [&](const std::string &x) {
    return x != "static" && std::find(b.begin(), b.end(), x) != b.end();
  });
}

// Annotations may sit on any redeclaration (header declaration, out-of-line definition): merge them.
template <class Fn> LifetimeSet mergedOver(const clang::FunctionDecl *fd, Fn &&get) {
  LifetimeSet out;
  for (const clang::FunctionDecl *r : fd->redecls())
    for (auto &n : get(r))
      if (std::find(out.begin(), out.end(), n) == out.end())
        out.push_back(std::move(n));
  return out;
}

inline LifetimeSet declReturnLifetimes(const clang::FunctionDecl *fd) { return mergedOver(fd, returnLifetimes); }
inline LifetimeSet declThisLifetimes(const clang::FunctionDecl *fd) { return mergedOver(fd, thisLifetimes); }

inline LifetimeSet declParamLifetimes(const clang::FunctionDecl *fd, unsigned i) {
  LifetimeSet out;
  for (const clang::FunctionDecl *r : fd->redecls())
    if (i < r->getNumParams())
      for (auto &n : paramLifetimes(r->getParamDecl(i)))
        if (std::find(out.begin(), out.end(), n) == out.end())
          out.push_back(std::move(n));
  return out;
}

// Whether argument `i` of a call to `fd` is tied to the result: RELOCO_LIFETIMEBOUND or a lifetime shared with
// RELOCO_LT_RETURN.
inline bool paramFlowsToReturn(const clang::FunctionDecl *fd, unsigned i) {
  if (i >= fd->getNumParams())
    return false;
  if (fd->getParamDecl(i)->hasAttr<clang::LifetimeBoundAttr>())
    return true;
  return sharesLifetime(declParamLifetimes(fd, i), declReturnLifetimes(fd));
}

} // namespace reloco::tidy
