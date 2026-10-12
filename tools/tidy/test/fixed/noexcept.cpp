// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-noexcept
void bad() noexcept;  // expect: reloco-noexcept
void good() noexcept;
struct s {
  void m() noexcept;  // expect: reloco-noexcept
  void n() noexcept;
  s() = default;
  ~s();
};
template <typename T> void tpl(T) noexcept(noexcept(T{}));
auto lambda = [] {};
// Deduction guides cannot carry an exception specification, so they are exempt.
template <typename T> struct box { box(T) noexcept; };
template <typename T> box(T) -> box<T>;
