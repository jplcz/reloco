// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-unused-result
#include <reloco/lifetime.hpp>

template <typename T> struct expected {};
expected<int> make() noexcept;
int plain() noexcept;
void f() noexcept {
  make();  // expect: reloco-unused-result
  plain();
  (void)make();
  auto r = make();
  (void)r;
}
