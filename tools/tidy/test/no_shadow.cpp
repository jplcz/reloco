// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-no-shadow
#include <reloco/lifetime.hpp>

void f(int p) noexcept {
  int a = 1;
  {
    int a = 2;  // expect: reloco-no-shadow
    (void)a;
  }
  for (int p = 0; p < 1; ++p) {  // expect: reloco-no-shadow
  }
  {
    int fresh = 3;
    (void)fresh;
  }
  (void)a; (void)p;
}
