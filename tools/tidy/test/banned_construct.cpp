// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-banned-construct
#include <cstring>
#include <reloco/lifetime.hpp>

void f(int *p, const void *q, char *d) noexcept {
  int *n = new int(1);  // expect: reloco-banned-construct
  delete n;  // expect: reloco-banned-construct
  auto *c = const_cast<int *>(p);  // expect: reloco-banned-construct
  auto r = reinterpret_cast<const char *>(q);  // expect: reloco-banned-construct
  auto s = (long)p;  // expect: reloco-banned-construct
  memcpy(d, r, 1);  // expect: reloco-banned-construct
  (void)c; (void)s;
}
RELOCO_UNSAFE_BUFFER_USAGE void unsafe_copy(char *d, const void *q) noexcept {
  memcpy(d, q, 1);
  (void)reinterpret_cast<const char *>(q);
}
