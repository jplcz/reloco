// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-explicit-conversion
#include <reloco/lifetime.hpp>

struct a {
  a(int) noexcept;  // expect: reloco-explicit-conversion
  explicit a(long) noexcept;
  a(int, int) noexcept;
  a(const a &) noexcept;
  operator bool() const noexcept;  // expect: reloco-explicit-conversion
  explicit operator int() const noexcept;
  // `explicit` must come after the attribute list, not before it.
  [[nodiscard]] operator char() const noexcept;  // expect: reloco-explicit-conversion
};
