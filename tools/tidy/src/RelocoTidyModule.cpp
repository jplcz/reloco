// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "Checks.h"

#include "clang-tidy/ClangTidyModule.h"

namespace reloco::tidy {

namespace {

class RelocoTidyModule : public clang::tidy::ClangTidyModule {
public:
  void addCheckFactories(clang::tidy::ClangTidyCheckFactories &f) override {
    f.registerCheck<RefQualifiedMemberCheck>("reloco-ref-qualified-member");
    f.registerCheck<NoRawPointerCheck>("reloco-no-raw-pointer");
    f.registerCheck<NoCArrayCheck>("reloco-no-c-array");
    f.registerCheck<NoexceptCheck>("reloco-noexcept");
    f.registerCheck<OutParamCheck>("reloco-out-param");
    f.registerCheck<UninitLocalCheck>("reloco-uninit-local");
    f.registerCheck<ImplicitCopyCheck>("reloco-implicit-copy");
    f.registerCheck<LifetimeAnnotationsCheck>("reloco-lifetime-annotations");
    f.registerCheck<PointerAnnotationCheck>("reloco-pointer-annotation");
    f.registerCheck<BannedStdTypeCheck>("reloco-banned-std-type");
    f.registerCheck<BannedConstructCheck>("reloco-banned-construct");
    f.registerCheck<BorrowCheck>("reloco-borrow-check");
  }
};

} // namespace

} // namespace reloco::tidy

// Registers the module when the plugin is loaded with `clang-tidy --load`.
static clang::tidy::ClangTidyModuleRegistry::Add<reloco::tidy::RelocoTidyModule>
    relocoTidyModule("reloco-module", "Reloco safe-coding rules.");

// Anchor so the linker keeps the registration when linked statically.
volatile int relocoTidyModuleAnchorSource = 0;
