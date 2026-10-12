// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-escaping-local
#include <reloco/lifetime.hpp>

// A tiny view over ints, standing in for reloco::span.
struct RELOCO_POINTER view {
  const int *data = nullptr;
  view() = default;
  explicit view(const int (&a)[4]) noexcept : data(a) {}
};
struct text {
  char buf[8] = {};
  const char *c_str() const & noexcept RELOCO_LIFETIMEBOUND { return buf; }
};
struct sink {
  int *p = nullptr;
  view v;
};
int g = 0;

void bad_out_pointer(int **out) noexcept {
  int x = 0;
  *out = &x;  // expect: reloco-escaping-local
}
void bad_out_member(sink *s) noexcept {
  int x = 0;
  s->p = &x;  // expect: reloco-escaping-local
}
void bad_ref_member(sink &s) noexcept {
  int x = 0;
  s.p = &x;  // expect: reloco-escaping-local
}
void bad_ref_to_pointer(int *&out) noexcept {
  int x = 0;
  out = &x;  // expect: reloco-escaping-local
}
void bad_array_decay(int **out) noexcept {
  int a[4] = {};
  *out = a;  // expect: reloco-escaping-local
}
void bad_view(sink &s) noexcept {
  int a[4] = {};
  s.v = view(a);  // expect: reloco-escaping-local
}
void bad_lifetimebound_call(const char **out) noexcept {
  text t;
  *out = t.c_str();  // expect: reloco-escaping-local
}
void bad_by_value_param(int **out, int v) noexcept {
  *out = &v;  // expect: reloco-escaping-local
}
void bad_nested_field(sink &s) noexcept {
  struct { int field = 0; } local;
  s.p = &local.field;  // expect: reloco-escaping-local
}

void ok_global(int **out) noexcept { *out = &g; }
void ok_forwarded_pointer(int **out, int *in) noexcept { *out = in; }
void ok_pointer_param_target(int **out, int *&ref) noexcept { *out = ref; }
void ok_local_pointer_to_external(int **out, int *in) noexcept {
  int *copy = in;
  *out = copy;
}
void ok_local_pointer_assignment() noexcept {
  int x = 0;
  int *p = nullptr;
  p = &x;
  (void)p;
}
void ok_value_store(int *out) noexcept {
  int x = 5;
  *out = x;
}
void ok_static_local(int **out) noexcept {
  static int x = 0;
  *out = &x;
}
void ok_pointer_rebind(int *out) noexcept {
  int x = 0;
  out = &x;
  (void)out;
}

// --- exemptions -----------------------------------------------------------
RELOCO_BORROW_EXEMPT void exempt_function(int **out) noexcept {
  int x = 0;
  *out = &x;
}
void exempt_by_nolint(int **out) noexcept {
  int x = 0;
  *out = &x;  // NOLINT(reloco-escaping-local): the pointer is consumed before this frame ends
}
RELOCO_UNSAFE_BUFFER_USAGE void unsafe_tier(int **out) noexcept {
  int x = 0;
  *out = &x;
}
