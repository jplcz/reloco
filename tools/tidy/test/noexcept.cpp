// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-noexcept
void bad();  // expect: reloco-noexcept
void good() noexcept;
struct s {
  void m();  // expect: reloco-noexcept
  void n() noexcept;
  s() = default;
  ~s();
};
template <typename T> void tpl(T) noexcept(noexcept(T{}));
auto lambda = [] {};
