// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "clang-tidy/ClangTidyCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reloco::tidy {

using namespace clang;
using namespace clang::ast_matchers;
using clang::tidy::ClangTidyCheck;
using clang::tidy::ClangTidyContext;

// Where the RELOCO_BEGIN/END_SAFE_CODE and RELOCO_BEGIN/END_UNSAFE_CODE markers (expanding to
// `#pragma region reloco_*_begin` / `#pragma endregion reloco_*_end`) place a source offset.
class SafeRegions {
public:
  enum class State { none, safe, unsafe };
  // Independent of State: inside `#pragma clang unsafe_buffer_usage begin/end`
  // (RELOCO_BEGIN/END_UNSAFE_BUFFER_USAGE).
  void recordUnsafeBuffer(const SourceManager &sm, SourceLocation loc, bool begin);
  [[nodiscard]] bool inUnsafeBuffer(const SourceManager &sm, SourceLocation loc) const;
  void record(const SourceManager &sm, SourceLocation loc, State state);
  [[nodiscard]] State stateAt(const SourceManager &sm, SourceLocation loc) const;

private:
  std::map<FileID, std::vector<std::pair<unsigned, State>>> events_;
  std::map<FileID, std::vector<std::pair<unsigned, bool>>> bufferEvents_;
};

// Base class shared by every reloco-* check. It centralises the policy that
// tests, examples and demos are free to do anything, while library headers
// and implementation files are held to the strict rules.
class RelocoCheck : public ClangTidyCheck {
public:
  RelocoCheck(llvm::StringRef name, ClangTidyContext *ctx);
  void storeOptions(clang::tidy::ClangTidyOptions::OptionMap &opts) override;
  void registerPPCallbacks(const SourceManager &sm, Preprocessor *pp, Preprocessor *moduleExpanderPP) override;

protected:
  // True when the diagnostic location must not be reported: invalid, system
  // header, a path matching one of the ExemptPaths substrings, inside a
  // RELOCO_BEGIN_UNSAFE_CODE region, or (SafeCodeMode: opt-in) outside every
  // RELOCO_BEGIN_SAFE_CODE region.
  [[nodiscard]] bool exempt(const SourceManager &sm, SourceLocation loc) const;

  // Checks that model Rust-style borrowing also honour Clang's unsafe_buffer_usage blocks.
  [[nodiscard]] virtual bool honorsUnsafeBufferUsage() const { return false; }

  // gsl::Pointer / gsl::Owner detection on the (canonical) record of a type.
  [[nodiscard]] static bool isPointerLike(QualType t);
  [[nodiscard]] static bool isOwnerLike(QualType t);
  [[nodiscard]] static bool recordIsPointer(const CXXRecordDecl *rd);
  [[nodiscard]] static bool recordIsOwner(const CXXRecordDecl *rd);

  // Raw pointer, reference or gsl::Pointer value: a type that aliases storage.
  [[nodiscard]] static bool isAliasing(QualType t);

  // Functions explicitly in the unsafe tier: [[clang::unsafe_buffer_usage]] or
  // named unsafe_*. Raw pointers and casts are tolerated there.
  [[nodiscard]] static bool inUnsafeTier(const Decl *d);

  // Attaches a "how to fix" note with a concrete before/after example to the preceding warning.
  void howToFix(SourceLocation loc, llvm::StringRef text);

  // Fix-it anchor: the end of ')' plus trailing const/volatile (and, with skipRef, '&'/'&&'), i.e. where a
  // ref-qualifier (skipRef=false) or noexcept (skipRef=true) is inserted. Invalid when it cannot be located safely.
  [[nodiscard]] static SourceLocation functionQualifierEnd(const FunctionDecl *fd, const SourceManager &sm,
                                                           const LangOptions &lo, bool skipRef,
                                                           bool skipNoexcept = false);

private:
  std::string exemptPathsOpt_;
  std::vector<std::string> exemptPaths_;
  std::string modeOpt_;
  bool optIn_ = false;
  std::shared_ptr<SafeRegions> regions_ = std::make_shared<SafeRegions>();
};

} // namespace reloco::tidy
