// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-member-init
#include <reloco/lifetime.hpp>

struct s {
  int a{};  // expect: reloco-member-init
  int b = 0;
  int c[2]{};  // expect: reloco-member-init
  int d{};
  int e;  // NOLINT(reloco-member-init): set by every constructor
};
struct n { int x = 0; };
struct t { n inner; };
