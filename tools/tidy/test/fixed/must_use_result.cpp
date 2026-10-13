// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-must-use-result
#include <reloco/lifetime.hpp>

template <typename T> struct expected {};
template <typename T> struct [[nodiscard]] expected_ok {};
struct error {};
[[nodiscard]] expected<int> bad() noexcept;  // expect: reloco-must-use-result
[[nodiscard]] expected<int> good() noexcept;
[[nodiscard]] error bad2() noexcept;  // expect: reloco-must-use-result
void unrelated() noexcept;

// A lambda's call operator is exempt: [[nodiscard]] on it is not portable before C++23 and its caller consumes the value.
void use_lambda() noexcept {
  auto fn = [](int v) noexcept -> expected<int> { (void)v; return {}; };
  (void)fn(1);
}
