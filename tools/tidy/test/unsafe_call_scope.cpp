// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-unsafe-call-scope
#include <reloco/lifetime.hpp>

RELOCO_UNSAFE_BUFFER_USAGE void raw_poke(int) noexcept;
void unsafe_helper() noexcept;
void bad() noexcept {
  raw_poke(1);  // expect: reloco-unsafe-call-scope
  unsafe_helper();  // expect: reloco-unsafe-call-scope
}
void unsafe_caller() noexcept {
  raw_poke(1);
  unsafe_helper();
}
void regions() noexcept {
  RELOCO_BEGIN_UNSAFE_CODE
  raw_poke(1);
  RELOCO_END_UNSAFE_CODE
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  raw_poke(2);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  raw_poke(3);  // NOLINT(reloco-unsafe-call-scope)
}
