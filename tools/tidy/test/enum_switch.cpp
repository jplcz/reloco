// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-enum-switch
#include <reloco/lifetime.hpp>

enum class e { a, b };
int f(e v) noexcept {
  switch (v) {  // expect: reloco-enum-switch
  case e::a: return 1;
  default: return 2;
  }
}
int g(e v) noexcept {
  switch (v) {  // expect: reloco-enum-switch
  case e::a: return 1;
  }
  return 0;
}
int h(e v) noexcept {
  switch (v) {
  case e::a: return 1;
  case e::b: return 2;
  }
  return 0;
}
