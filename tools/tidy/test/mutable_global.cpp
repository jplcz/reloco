// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-mutable-global
#include <reloco/lifetime.hpp>

int counter;  // expect: reloco-mutable-global
const int limit = 3;
constexpr int cap = 4;
struct atomic_int { int v; };
atomic_int hits;
struct s { static int shared; };  // expect: reloco-mutable-global
int f() noexcept {
  static int calls = 0;  // expect: reloco-mutable-global
  return ++calls;
}
