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
int with_default_all_listed(e v) noexcept {
  switch (v) {
  case e::a: return 1;
  case e::b: return 2;
  default: return 3;  // allowed: every enumerator is listed, default only guards out-of-range values
  }
}
int default_only(e v) noexcept {
  switch (v) {  // expect: reloco-enum-switch
  default: return 3;
  }
}
int default_suppressed(e v) noexcept {
  switch (v) {  // NOLINT(reloco-enum-switch)
  case e::a: return 1;
  default: return 2;
  }
}
int h(e v) noexcept {
  switch (v) {
  case e::a: return 1;
  case e::b: return 2;
  }
  return 0;
}

int integer_no_default(int v) noexcept {
  switch (v) { // expect: reloco-enum-switch
  case 1: return 1;
  case 2: return 2;
  }
  return 0;
}
int integer_with_default(int v) noexcept {
  switch (v) {
  case 1: return 1;
  default: return 0;
  }
}
