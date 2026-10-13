// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-unused-result
#include <reloco/lifetime.hpp>

template <typename T> struct expected {
  expected() noexcept = default;
  expected(expected &&) noexcept = default;
  expected &operator=(expected &&other) noexcept { (void)other; return *this; }
  expected &assign() noexcept { return *this; }
};
expected<int> make() noexcept;
int plain() noexcept;
void f() noexcept {
  make();  // expect: reloco-unused-result
  plain();
  (void)make();
  auto r = make();
  (void)r;
}

// The implicit move assignment calls expected::operator= (a reference-returning member call): not a discarded value.
struct holder {
  expected<int> status;
};
void g(holder &a, holder &b) noexcept {
  a = static_cast<holder &&>(b);
  a.status.assign();  // returns a reference: nothing is lost
}
