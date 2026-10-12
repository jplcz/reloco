// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-pointer-annotation
#include <reloco/lifetime.hpp>

struct RELOCO_POINTER view_ok { int *p; };
struct RELOCO_OWNER owner_ok { int *p; };
struct missing { int *p; };  // expect: reloco-pointer-annotation
struct refs { int &r; };  // expect: reloco-pointer-annotation
struct plain { int v; void (*fn)() noexcept; };
struct holds_view { view_ok v; };  // expect: reloco-pointer-annotation
