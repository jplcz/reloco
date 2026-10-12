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

// A member typedef is not nameable at the use site, so the fix-it spells the canonical type.
struct with_size_type {
  using size_type = unsigned long;
  int at(size_type i) const noexcept;
};
int use_size_type(const with_size_type &w, int i) noexcept { return w.at(i); }  // expect: reloco-implicit-conversion

// A non-negative constant mask bounds the result, so the nibble idiom is not a lossy conversion.
unsigned long nibble(int b) noexcept { return static_cast<unsigned long>(0) + ((b >> 4) & 0x0F); }
unsigned long use_nibble(const unsigned long (&t)[16], int b) noexcept { return t[(b >> 4) & 0x0F] + t[b & 0x0F]; }
