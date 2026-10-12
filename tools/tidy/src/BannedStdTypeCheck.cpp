// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"

namespace reloco::tidy {

namespace {

struct Ban {
  const char *name;
  const char *hint;
};

// Same policy as scripts/check-no-std-types.py, extended with the safe-view types,
// but decided on the AST (aliases such as std::string are caught as well).
constexpr Ban bans[] = {
    {"vector", "reloco::vector/inline_vector"},
    {"deque", "reloco::vec_deque"},
    {"map", "reloco::tree_map/flat_map"},
    {"multimap", "reloco::tree_map"},
    {"set", "reloco::tree_set/flat_set"},
    {"multiset", "reloco::tree_set"},
    {"unordered_map", "reloco::flat_hash_map"},
    {"unordered_set", "reloco::flat_hash_set"},
    {"list", "reloco::intrusive_c_list"},
    {"forward_list", "reloco::intrusive_c_slist"},
    {"stack", "reloco::vector"},
    {"queue", "reloco::vec_deque"},
    {"priority_queue", "reloco::binary_heap"},
    {"basic_string", "reloco::inline_string/string"},
    {"string", "reloco::inline_string/string"},
    {"wstring", "reloco::string"},
    {"string_view", "reloco::string_view"},
    {"basic_string_view", "reloco::string_view"},
    {"span", "reloco::span"},
    {"array", "reloco::array"},
    {"optional", "reloco::optional"},
    {"nullopt_t", "reloco::nullopt"},
    {"variant", "reloco::variant"},
    {"any", "reloco::any"},
    {"function", "reloco::function_ref/inplace_function"},
    {"shared_ptr", "reloco::shared_ptr"},
    {"unique_ptr", "reloco::unique_ptr"},
    {"weak_ptr", "reloco::shared_ptr"},
    {"make_shared", "reloco::shared_ptr"},
    {"make_unique", "reloco::unique_ptr"},
    {"source_location", "reloco::call_location_ref"},
    {"exception", "reloco::error/expected"},
    {"bad_alloc", "reloco::error/expected"},
    {"runtime_error", "reloco::error/expected"},
    {"logic_error", "reloco::error/expected"},
    {"out_of_range", "reloco::error/expected"},
    {"invalid_argument", "reloco::error/expected"},
    {"system_error", "reloco::error"},
    {"cout", "microfmt"},
    {"cerr", "microfmt"},
    {"cin", "microfmt"},
    {"ostream", "microfmt"},
    {"istream", "microfmt"},
    {"stringstream", "microfmt"},
    {"ostringstream", "microfmt"},
    {"ifstream", "microfmt"},
    {"ofstream", "microfmt"},
    {"mutex", "reloco::mutex"},
    {"recursive_mutex", "reloco::mutex"},
    {"shared_mutex", "reloco::rw_lock"},
    {"thread", "reloco::thread"},
    {"condition_variable", "reloco::park/futex"},
    {"unique_lock", "reloco::unique_lock"},
    {"shared_lock", "reloco::shared_lock"},
    {"lock_guard", "reloco::unique_lock"},
    {"scoped_lock", "reloco::unique_lock"},
};

const llvm::StringMap<const char *> &banTable() {
  static const llvm::StringMap<const char *> table = [] {
    llvm::StringMap<const char *> t;
    for (const Ban &b : bans)
      t[b.name] = b.hint;
    return t;
  }();
  return table;
}

const NamedDecl *declOf(QualType t) {
  // A project alias (e.g. reloco::string_view) is spelled legitimately even when it forwards to std.
  if (const auto *tdt = t->getAs<TypedefType>())
    if (!tdt->getDecl()->isInStdNamespace())
      return nullptr;
  if (const auto *tst = t->getAs<TemplateSpecializationType>()) {
    if (tst->isTypeAlias()) {
      const TemplateDecl *td = tst->getTemplateName().getAsTemplateDecl();
      if (td && !td->isInStdNamespace())
        return nullptr;
    }
  }
  t = t.getCanonicalType();
#if RELOCO_TIDY_LLVM_MAJOR == 21
  if (const auto *tt = t->getAs<TagType>())
    return tt->getOriginalDecl();
#else
  if (const auto *tt = t->getAs<TagType>())
    return tt->getDecl();
#endif
  if (const auto *tst = t->getAs<TemplateSpecializationType>())
    if (const TemplateDecl *td = tst->getTemplateName().getAsTemplateDecl())
      return td;
  return nullptr;
}

} // namespace

// Honours the same `// std-interop-ok|begin|end|file` comment markers as scripts/check-no-std-types.py, so a
// deliberate libstdc++/libc++ interop point is marked once and accepted by both tools.
static bool markedStdInterop(const SourceManager &sm, SourceLocation loc) {
  loc = sm.getExpansionLoc(loc);
  const auto [fid, offset] = sm.getDecomposedLoc(loc);
  bool invalid = false;
  const llvm::StringRef buf = sm.getBufferData(fid, &invalid);
  if (invalid)
    return false;
  if (buf.contains("std-interop-file"))
    return true;
  const unsigned line = sm.getLineNumber(fid, offset);
  unsigned n = 0;
  bool inBlock = false;
  for (llvm::StringRef text : llvm::split(buf, '\n')) {
    ++n;
    const size_t c = text.find("//");
    const llvm::StringRef comment = c == llvm::StringRef::npos ? llvm::StringRef() : text.substr(c);
    if (comment.contains("std-interop-begin"))
      inBlock = true;
    else if (comment.contains("std-interop-end"))
      inBlock = false;
    if (n == line)
      return inBlock || comment.contains("std-interop-ok");
    if (n > line)
      break;
  }
  return false;
}

void BannedStdTypeCheck::registerMatchers(MatchFinder *finder) {
  finder->addMatcher(typeLoc().bind("type"), this);
  finder->addMatcher(declRefExpr(to(namedDecl(isInStdNamespace()))).bind("ref"), this);
}

void BannedStdTypeCheck::check(const MatchFinder::MatchResult &result) {
  const NamedDecl *nd = nullptr;
  SourceLocation loc;
  if (const auto *tl = result.Nodes.getNodeAs<TypeLoc>("type")) {
    // Qualified and unqualified sub-locs of one spelling report the same location and are deduplicated.
    // A template parameter spelled `T` that was instantiated with a std type is the caller's choice, not this code's.
    for (QualType t = tl->getType(); !t.isNull();) {
      if (llvm::isa<SubstTemplateTypeParmType>(t.getTypePtr()))
        return;
      const QualType next = t.getSingleStepDesugaredType(*result.Context);
      if (next == t)
        break;
      t = next;
    }
    nd = declOf(tl->getType());
    loc = tl->getBeginLoc();
  } else if (const auto *dre = result.Nodes.getNodeAs<DeclRefExpr>("ref")) {
    nd = dre->getDecl();
    loc = dre->getBeginLoc();
  }
  if (!nd || !nd->isInStdNamespace() || !nd->getIdentifier() || exempt(*result.SourceManager, loc) ||
      markedStdInterop(*result.SourceManager, loc))
    return;
  const auto it = banTable().find(nd->getName());
  if (it == banTable().end())
    return;
  diag(loc, "std::%0 is banned; use %1") << nd->getName() << it->second;
}

} // namespace reloco::tidy
