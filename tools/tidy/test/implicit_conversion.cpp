// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-implicit-conversion
#include <reloco/lifetime.hpp>

void f(int i, unsigned u, long l, double d, float fl, unsigned char c) noexcept {
  short a = i;  // expect: reloco-implicit-conversion
  unsigned b = i;  // expect: reloco-implicit-conversion
  int e = u;  // expect: reloco-implicit-conversion
  int g = d;  // expect: reloco-implicit-conversion
  float h = d;  // expect: reloco-implicit-conversion
  long ok1 = i;
  int ok2 = c;
  double ok3 = fl;
  short ok4 = 5;
  unsigned char ok5 = 255;
  long ok6 = static_cast<long>(u);
  short ok7 = static_cast<short>(i);
  (void)a; (void)b; (void)e; (void)g; (void)h; (void)ok1; (void)ok2; (void)ok3; (void)ok4; (void)ok5; (void)ok6; (void)ok7; (void)l;
}
