// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "RelocoCheckBase.h"

#include "clang/AST/Attr.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include <algorithm>
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace reloco::tidy {

namespace {
constexpr const char *default_exempt_paths = "/tests/;/test/;/examples/;/example/;/demos/;/benchmarks/;/_deps/;"
                                             "/third_party/;/build/;/build-";
}

// Recognises the region markers whether written as `#pragma region reloco_safe_begin`, as
// `_Pragma("region ...")`, or through the RELOCO_BEGIN/END_*_CODE macros.
class RegionCallbacks : public PPCallbacks {
public:
  RegionCallbacks(const SourceManager &sm, const LangOptions &lo, std::shared_ptr<SafeRegions> regions)
      : sm_(sm), lo_(lo), regions_(std::move(regions)) {}

  void PragmaDirective(SourceLocation loc, PragmaIntroducerKind) override {
    std::string text;
    for (SourceLocation l = loc; l.isMacroID(); l = sm_.getImmediateMacroCallerLoc(l)) {
      text += Lexer::getImmediateMacroName(l, sm_, lo_).str();
      text += ' ';
    }
    const SourceLocation exp = sm_.getExpansionLoc(loc);
    bool invalid = false;
    const llvm::StringRef buf = sm_.getBufferData(sm_.getFileID(exp), &invalid);
    if (invalid)
      return;
    const auto [fid, off] = sm_.getDecomposedLoc(exp);
    (void)fid;
    const llvm::StringRef line = buf.substr(off).take_until([](char c) { return c == '\n'; });
    text += line.str();

    if (text.find("unsafe_buffer_usage begin") != std::string::npos ||
        text.find("RELOCO_BEGIN_UNSAFE_BUFFER_USAGE") != std::string::npos)
      regions_->recordUnsafeBuffer(sm_, exp, true);
    else if (text.find("unsafe_buffer_usage end") != std::string::npos ||
             text.find("RELOCO_END_UNSAFE_BUFFER_USAGE") != std::string::npos)
      regions_->recordUnsafeBuffer(sm_, exp, false);

    using State = SafeRegions::State;
    if (text.find("reloco_safe_begin") != std::string::npos || text.find("RELOCO_BEGIN_SAFE_CODE") != std::string::npos)
      regions_->record(sm_, exp, State::safe);
    else if (text.find("reloco_unsafe_begin") != std::string::npos ||
             text.find("RELOCO_BEGIN_UNSAFE_CODE") != std::string::npos)
      regions_->record(sm_, exp, State::unsafe);
    else if (text.find("reloco_safe_end") != std::string::npos || text.find("reloco_unsafe_end") != std::string::npos ||
             text.find("RELOCO_END_SAFE_CODE") != std::string::npos ||
             text.find("RELOCO_END_UNSAFE_CODE") != std::string::npos)
      regions_->record(sm_, exp, State::none);
  }

private:
  const SourceManager &sm_;
  const LangOptions &lo_;
  std::shared_ptr<SafeRegions> regions_;
};

void SafeRegions::record(const SourceManager &sm, SourceLocation loc, State state) {
  const auto [fid, off] = sm.getDecomposedExpansionLoc(loc);
  events_[fid].emplace_back(off, state);
}

void SafeRegions::recordUnsafeBuffer(const SourceManager &sm, SourceLocation loc, bool begin) {
  const auto [fid, off] = sm.getDecomposedExpansionLoc(loc);
  bufferEvents_[fid].emplace_back(off, begin);
}

bool SafeRegions::inUnsafeBuffer(const SourceManager &sm, SourceLocation loc) const {
  const auto [fid, off] = sm.getDecomposedExpansionLoc(loc);
  const auto it = bufferEvents_.find(fid);
  if (it == bufferEvents_.end())
    return false;
  const auto &ev = it->second;
  const auto pos = std::upper_bound(ev.begin(), ev.end(), off, [](unsigned o, const auto &e) { return o < e.first; });
  return pos != ev.begin() && std::prev(pos)->second;
}

SafeRegions::State SafeRegions::stateAt(const SourceManager &sm, SourceLocation loc) const {
  const auto [fid, off] = sm.getDecomposedExpansionLoc(loc);
  const auto it = events_.find(fid);
  if (it == events_.end())
    return State::none;
  const auto &ev = it->second;
  // Last marker at or before the offset decides; markers are recorded in preprocessing order.
  const auto pos = std::upper_bound(ev.begin(), ev.end(), off, [](unsigned o, const auto &e) { return o < e.first; });
  return pos == ev.begin() ? State::none : std::prev(pos)->second;
}

RelocoCheck::RelocoCheck(llvm::StringRef name, ClangTidyContext *ctx)
    : ClangTidyCheck(name, ctx), exemptPathsOpt_(Options.get("ExemptPaths", default_exempt_paths).str()),
      modeOpt_(Options.get("SafeCodeMode", "all").str()), optIn_(modeOpt_ == "opt-in") {
  llvm::SmallVector<llvm::StringRef, 8> parts;
  llvm::StringRef(exemptPathsOpt_).split(parts, ';', -1, false);
  for (auto p : parts)
    exemptPaths_.emplace_back(p.str());
}

void RelocoCheck::storeOptions(clang::tidy::ClangTidyOptions::OptionMap &opts) {
  Options.store(opts, "ExemptPaths", exemptPathsOpt_);
  Options.store(opts, "SafeCodeMode", modeOpt_);
}

void RelocoCheck::registerPPCallbacks(const SourceManager &sm, Preprocessor *pp, Preprocessor *) {
  pp->addPPCallbacks(std::make_unique<RegionCallbacks>(sm, pp->getLangOpts(), regions_));
}

bool RelocoCheck::exempt(const SourceManager &sm, SourceLocation loc) const {
  if (loc.isInvalid())
    return true;
  loc = sm.getExpansionLoc(loc);
  if (sm.isInSystemHeader(loc))
    return true;
  const llvm::StringRef file = sm.getFilename(loc);
  if (file.empty())
    return true;
  for (const auto &p : exemptPaths_)
    if (file.contains(p))
      return true;
  if (honorsUnsafeBufferUsage() && regions_->inUnsafeBuffer(sm, loc))
    return true;
  const auto state = regions_->stateAt(sm, loc);
  if (state == SafeRegions::State::unsafe)
    return true;
  return optIn_ && state != SafeRegions::State::safe;
}

bool RelocoCheck::recordIsPointer(const CXXRecordDecl *rd) {
  if (!rd || !rd->hasDefinition())
    return false;
  return rd->hasAttr<PointerAttr>();
}

bool RelocoCheck::recordIsOwner(const CXXRecordDecl *rd) {
  if (!rd || !rd->hasDefinition())
    return false;
  return rd->hasAttr<OwnerAttr>();
}

bool RelocoCheck::isPointerLike(QualType t) {
  return recordIsPointer(t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
}

bool RelocoCheck::isOwnerLike(QualType t) {
  return recordIsOwner(t.getNonReferenceType().getCanonicalType()->getAsCXXRecordDecl());
}

bool RelocoCheck::isAliasing(QualType t) {
  if (t.isNull())
    return false;
  if (t->isPointerType() || t->isReferenceType())
    return true;
  return !t->isDependentType() && isPointerLike(t);
}

SourceLocation RelocoCheck::functionQualifierEnd(const FunctionDecl *fd, const SourceManager &sm, const LangOptions &lo,
                                                 bool skipRef) {
  const TypeSourceInfo *tsi = fd->getTypeSourceInfo();
  if (!tsi)
    return {};
  const auto ftl = tsi->getTypeLoc().getAsAdjusted<FunctionTypeLoc>();
  if (!ftl || ftl.getRParenLoc().isInvalid() || ftl.getRParenLoc().isMacroID())
    return {};
  SourceLocation prev = ftl.getRParenLoc();
  for (;;) {
    const auto tok = Lexer::findNextToken(prev, sm, lo);
    if (!tok || tok->getLocation().isMacroID())
      break;
    // Raw lexing yields keywords as identifiers.
    const bool cv = tok->is(tok::raw_identifier) && (tok->getRawIdentifier() == "const" || tok->getRawIdentifier() == "volatile");
    const bool ref = skipRef && tok->isOneOf(tok::amp, tok::ampamp);
    if (!cv && !ref)
      break;
    prev = tok->getLocation();
  }
  return Lexer::getLocForEndOfToken(prev, 0, sm, lo);
}

void RelocoCheck::howToFix(SourceLocation loc, llvm::StringRef text) {
  diag(loc, "how to fix: %0", DiagnosticIDs::Note) << text;
}

bool RelocoCheck::inUnsafeTier(const Decl *d) {
  for (const Decl *c = d; c && !llvm::isa<TranslationUnitDecl>(c);
       c = Decl::castFromDeclContext(c->getDeclContext())) {
    if (c->hasAttr<UnsafeBufferUsageAttr>())
      return true;
    if (const auto *nd = llvm::dyn_cast<NamedDecl>(c))
      if (nd->getIdentifier() && nd->getName().starts_with("unsafe_"))
        return true;
  }
  return false;
}

} // namespace reloco::tidy
