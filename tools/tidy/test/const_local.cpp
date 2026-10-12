// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-const-local
#include <reloco/lifetime.hpp>

void take(int &) noexcept;
void f() noexcept {
  int a = 1;  // expect: reloco-const-local
  int b = 2;
  int c = 3;
  const int d = 4;
  b += 1;
  take(c);
  (void)a; (void)b; (void)d;
}

void asm_output() {
  int out = 0; // written by the asm output operand: not const
  asm volatile("" : "=r"(out));
  (void)out;
}

struct movable { movable(); movable(const movable &); movable(movable &&); ~movable(); };
movable make();
movable returned() {
  movable m = make(); // returned by name: const would force a copy
  return m;
}

struct ranged { int *begin(); int *end(); };
ranged make_ranged();
void range_for_mutates() {
  ranged r = make_ranged(); // begin()/end() are non-const: not const-eligible
  for (int x : r)
    (void)x;
}
