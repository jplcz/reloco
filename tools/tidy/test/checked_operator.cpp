// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-checked-operator
#include <reloco/lifetime.hpp>

struct unchecked {
  int &operator[](int i) & noexcept;  // expect: reloco-checked-operator
  int operator*(int) const noexcept; // binary-looking member, not guarded
};
struct checked {
  RELOCO_CHECKED int &operator[](int i) & noexcept { return v[i < 0 ? 0 : i & 3]; }
  RELOCO_CHECKED const int &operator*() const & noexcept { return v[0]; }
  int v[4] = {};  // NOLINT(reloco-no-c-array)
};
struct RELOCO_CHECKED whole {
  int &operator[](int) & noexcept;
  int &operator*() & noexcept;
};
int use(unchecked &u, checked &c, whole &w) noexcept {
  int x = u[1];  // expect: reloco-checked-operator
  return x + c[2] + *c + w[0] + *w;
}
void unsafe_use(unchecked &u) noexcept { (void)u[1]; }
