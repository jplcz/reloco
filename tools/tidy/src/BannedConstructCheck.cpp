// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "llvm/ADT/StringSet.h"

namespace reloco::tidy {

namespace {

bool isCFunction(const FunctionDecl *fn) {
  static const llvm::StringSet<> names = {
      "memcpy", "memmove", "memset", "memcmp", "memchr", "strlen", "strcpy", "strncpy", "strcat", "strncat",
      "strcmp", "strncmp", "strchr", "strstr", "sprintf", "snprintf", "vsprintf", "vsnprintf", "malloc",
      "calloc", "realloc", "free", "alloca", "atoi", "atol", "atoll", "atof", "strtol", "strtoul", "gets",
      "abort", "exit", "qsort", "bsearch"};
  if (!fn->getIdentifier() || !names.contains(fn->getName()))
    return false;
  const DeclContext *dc = fn->getDeclContext()->getRedeclContext();
  return dc->isTranslationUnit() || fn->isInStdNamespace();
}

} // namespace

void BannedConstructCheck::registerMatchers(MatchFinder *finder) {
  const auto enclosing = hasAncestor(functionDecl().bind("encl"));
  finder->addMatcher(cxxThrowExpr(optionally(enclosing)).bind("throw"), this);
  finder->addMatcher(cxxTryStmt(optionally(enclosing)).bind("try"), this);
  finder->addMatcher(cxxNewExpr(unless(hasAnyPlacementArg(anything())), optionally(enclosing)).bind("new"), this);
  finder->addMatcher(cxxDeleteExpr(optionally(enclosing)).bind("delete"), this);
  finder->addMatcher(cxxConstCastExpr(optionally(enclosing)).bind("const_cast"), this);
  finder->addMatcher(cxxReinterpretCastExpr(optionally(enclosing)).bind("reinterpret_cast"), this);
  finder->addMatcher(cxxDynamicCastExpr(optionally(enclosing)).bind("dynamic_cast"), this);
  finder->addMatcher(cStyleCastExpr(unless(hasCastKind(CK_ToVoid)), optionally(enclosing)).bind("c_cast"), this);
  finder->addMatcher(callExpr(callee(functionDecl(unless(cxxMethodDecl())).bind("callee")), optionally(enclosing)).bind("cfn"),
                     this);
}

void BannedConstructCheck::check(const MatchFinder::MatchResult &result) {
  static constexpr struct {
    const char *id;
    const char *msg;
    bool unsafeTierAllowed;
  } kinds[] = {
      {"throw", "exceptions are banned; return reloco::expected/error", false},
      {"try", "exceptions are banned; return reloco::expected/error", false},
      {"new", "naked new is banned; use reloco allocators/containers", false},
      {"delete", "naked delete is banned; use reloco allocators/containers", false},
      {"const_cast", "const_cast defeats const-correctness; allowed only in unsafe_* functions", true},
      {"reinterpret_cast", "reinterpret_cast is allowed only in unsafe_* functions; use reloco bytes/span helpers", true},
      {"dynamic_cast", "RTTI is banned", false},
      {"c_cast", "C-style cast; use static_cast or a checked conversion", true},
  };

  const FunctionDecl *encl = result.Nodes.getNodeAs<FunctionDecl>("encl");
  auto report = [&](const Stmt *s, const char *msg, bool unsafeOk) {
    if (exempt(*result.SourceManager, s->getBeginLoc()))
      return;
    if (unsafeOk && encl && inUnsafeTier(encl))
      return;
    diag(s->getBeginLoc(), "%0") << msg;
    howToFix(s->getBeginLoc(), "return reloco::expected<T, E>/result<T> instead of throwing; own memory with reloco containers or "
                               "unique_ptr instead of new/delete; replace casts with static_cast or a checked conversion; replace "
                               "memcpy/strlen with span/string_view operations. If it is genuinely required, confine it to an "
                               "unsafe_* function or a RELOCO_BEGIN_UNSAFE_CODE region with a comment saying why it is sound");
  };

  for (const auto &k : kinds)
    if (const auto *s = result.Nodes.getNodeAs<Stmt>(k.id)) {
      report(s, k.msg, k.unsafeTierAllowed);
      return;
    }

  if (const auto *call = result.Nodes.getNodeAs<CallExpr>("cfn")) {
    const auto *callee = result.Nodes.getNodeAs<FunctionDecl>("callee");
    if (callee && isCFunction(callee))
      report(call, "C library memory/string function; use reloco span/bytes/string helpers or an unsafe_* wrapper",
             true);
  }
}

} // namespace reloco::tidy
