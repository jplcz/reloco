// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-aliased-arguments
#include <reloco/lifetime.hpp>

struct pair_t {
  int a = 0;
  int b = 0;
  void set(int x) & noexcept { a = x; }
  void copy_from(const pair_t &o) & noexcept { a = o.a; }
  void assign_ref(int &dst, const int &src) & noexcept { dst = src; }
  int get() const & noexcept { return a; }
  const int &cref() const & noexcept RELOCO_LIFETIMEBOUND { return a; }
};

void two_mut(int &, int &) noexcept;
void mut_const(int &, const int &) noexcept;
void two_const(const int &, const int &) noexcept;
void ptr_and_ref(int *, const int &) noexcept;
void by_value(int &, int) noexcept;
void swap_pair(pair_t &, pair_t &) noexcept;

void bad_two_mut() noexcept {
  int x = 0;
  two_mut(x, x);  // expect: reloco-aliased-arguments
}
void bad_mut_const() noexcept {
  int x = 0;
  mut_const(x, x);  // expect: reloco-aliased-arguments
}
void bad_field_vs_whole() noexcept {
  pair_t p;
  swap_pair(p, p);  // expect: reloco-aliased-arguments
}
void bad_pointer_arg() noexcept {
  int x = 0;
  ptr_and_ref(&x, x);  // expect: reloco-aliased-arguments
}
void bad_receiver() noexcept {
  pair_t p;
  p.copy_from(p);  // expect: reloco-aliased-arguments
}
void bad_receiver_field() noexcept {
  pair_t p;
  p.assign_ref(p.a, p.a);  // expect: reloco-aliased-arguments
}
void bad_lifetimebound_result() noexcept {
  pair_t p;
  p.assign_ref(p.a, p.cref());  // expect: reloco-aliased-arguments
}

void ok_two_const() noexcept {
  int x = 0;
  two_const(x, x);
}
void ok_disjoint_fields() noexcept {
  pair_t p;
  mut_const(p.a, p.b);
}
void bad_receiver_overlaps_field() noexcept {
  pair_t p;
  p.assign_ref(p.a, p.b);  // expect: reloco-aliased-arguments
}
void ok_different_objects() noexcept {
  pair_t p, q;
  p.copy_from(q);
  swap_pair(p, q);
}
void ok_by_value() noexcept {
  int x = 0;
  by_value(x, x);
}
void ok_by_value_receiver() noexcept {
  pair_t p;
  p.set(p.get());  // two-phase: the argument is a copied int
}
void ok_array_elements() noexcept {
  int v[2] = {};
  two_mut(v[0], v[1]);
}
void ok_const_receiver() noexcept {
  pair_t p;
  mut_const(p.b, p.get());
}

// --- exemptions -----------------------------------------------------------
RELOCO_BORROW_EXEMPT void exempt_function() noexcept {
  int x = 0;
  two_mut(x, x);
}
void exempt_by_nolint() noexcept {
  int x = 0;
  two_mut(x, x);  // NOLINT(reloco-aliased-arguments): callee documents aliasing support
}
RELOCO_UNSAFE_BUFFER_USAGE void unsafe_tier() noexcept {
  int x = 0;
  two_mut(x, x);
}
