// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-must-use-result
#include <reloco/lifetime.hpp>

template <typename T> struct expected {};
template <typename T> struct [[nodiscard]] expected_ok {};
struct error {};
expected<int> bad() noexcept;  // expect: reloco-must-use-result
[[nodiscard]] expected<int> good() noexcept;
error bad2() noexcept;  // expect: reloco-must-use-result
void unrelated() noexcept;
