// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-banned-low-level
#include <reloco/lifetime.hpp>

union u { int a; float b; };  // expect: reloco-banned-low-level
volatile int g_reg;  // expect: reloco-banned-low-level
struct s { volatile int mmio; };  // expect: reloco-banned-low-level
volatile int ok_reg;  // NOLINT(reloco-banned-low-level): MMIO
void f() noexcept {
  goto end;  // expect: reloco-banned-low-level
end:
  return;
}
