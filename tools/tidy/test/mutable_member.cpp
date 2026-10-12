// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-mutable-member
#include <reloco/lifetime.hpp>

struct atomic_int {
  int v = 0;
};
struct cache {
  mutable int hits_ = 0; // expect: reloco-mutable-member
  mutable atomic_int ok_;
  int plain_ = 0;
};
