// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-move-const
#include <utility>

struct heavy {
  heavy() = default;
  heavy(const heavy &) noexcept {}
  heavy(heavy &&) noexcept {}
};

void run() noexcept {
  const heavy c;
  heavy m;
  const int n = 3;
  heavy a = std::move(c); // expect: reloco-move-const
  heavy b = std::move(m);
  int i = std::move(n);
  (void)a; (void)b; (void)i;
}
