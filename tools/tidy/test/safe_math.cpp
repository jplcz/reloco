// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-safe-math
#include <reloco/lifetime.hpp>

int plain(int a, int b) noexcept { return a + b; }
RELOCO_SAFE_MATH int strict(int a, int b) noexcept {
  int c = a + b;  // expect: reloco-safe-math
  c *= 2;  // expect: reloco-safe-math
  const int k = 2 + 3;
  return c - k + (a < b);  // expect: reloco-safe-math
}
struct RELOCO_SAFE_MATH m {
  int f(int a) const & noexcept { return -a; }  // expect: reloco-safe-math
};
