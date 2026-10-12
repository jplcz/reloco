// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-implicit-copy
#include <reloco/lifetime.hpp>

struct plain {
  int v = 0;
};
struct RELOCO_COPYABLE point {
  int x = 0;
  int y = 0;
};
struct RELOCO_POINTER view {
  const int *p = nullptr;
};
template <typename T> struct RELOCO_COPYABLE boxed {
  T v{};
};
struct holder {
  plain p;
  holder(const plain &o) noexcept : p(o) {}  // direct member init: explicit clone
};

void by_value(plain) noexcept;
void by_value_point(point) noexcept;
plain make() noexcept;

void cases(plain a, point pt, view vw, boxed<int> bx) noexcept {
  plain b = a;  // expect: reloco-implicit-copy
  by_value(a);  // expect: reloco-implicit-copy
  plain c;
  c = a;  // expect: reloco-implicit-copy
  plain arr[2] = {};
  for (plain e : arr) {  // expect: reloco-implicit-copy
    (void)e;
  }
  plain d(a);            // explicit clone
  plain e2{a};           // explicit clone
  plain f = plain(a);    // explicit clone
  plain g = static_cast<plain>(a);
  plain h;
  plain k = make();      // prvalue, no copy
  point p2 = pt;         // RELOCO_COPYABLE
  by_value_point(pt);
  view v2 = vw;          // views are Copy
  boxed<int> b2 = bx;    // marker on the primary template
  plain nl = a;          // NOLINT(reloco-implicit-copy)
  (void)b; (void)c; (void)d; (void)e2; (void)f; (void)g; (void)h; (void)k; (void)p2; (void)v2; (void)b2; (void)nl;
}

struct copy_only_assign {
  copy_only_assign() = default;
  copy_only_assign(const copy_only_assign &) noexcept = default;
  copy_only_assign &operator=(const copy_only_assign &) noexcept = default;
};
copy_only_assign make_copy_only() noexcept;
void assign_from_temporary(copy_only_assign &dst) noexcept {
  dst = make_copy_only(); // fine: a temporary has nothing to move out of beyond the copy the type forces
}

// Generic code copying the caller's own T (or a type built from it) is the caller's choice, not the template's.
struct copyable_payload {
  copyable_payload() noexcept;
  copyable_payload(const copyable_payload &) noexcept;
};
template <typename T> struct holder {
  T value;
  void set(const T &v) noexcept { value = v; }
};
void use_holder(holder<copyable_payload> &h, const copyable_payload &p) noexcept { h.set(p); }
