// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-no-c-array
#include <reloco/lifetime.hpp>

struct s {
  int bad[4] = {};  // expect: reloco-no-c-array
  int ok;
  char early_boot[64];  // NOLINT(reloco-no-c-array): lives in .bss before constructors run
};
int global_bad[8];  // expect: reloco-no-c-array

void f() noexcept {
  int local[3] = {};  // expect: reloco-no-c-array
  RELOCO_BEGIN_UNSAFE_CODE
  int tolerated[3] = {};
  RELOCO_END_UNSAFE_CODE
  (void)local; (void)tolerated;
}
RELOCO_UNSAFE_BUFFER_USAGE void unsafe_tier() noexcept {
  int tolerated[3] = {};
  (void)tolerated;
}
