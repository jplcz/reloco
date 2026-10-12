// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-out-param
#include <reloco/lifetime.hpp>

void fill(int *p) noexcept;
void fill_ref(int &r) noexcept;

void ok_ptr(int *x RELOCO_OUT) noexcept { *x = 1; }
void ok_ref(int &x RELOCO_OUT) noexcept { x = 1; }
void ok_branches(int *x RELOCO_OUT, bool c) noexcept {
  if (c)
    *x = 1;
  else
    *x = 2;
}
void ok_handoff(int *x RELOCO_OUT) noexcept { fill(x); }
void ok_ref_handoff(int &x RELOCO_OUT) noexcept { fill_ref(x); }
void ok_early_return(int *x RELOCO_OUT, bool c) noexcept {
  *x = 0;
  if (c)
    return;
  *x += 1;
}

void missing_else(int *x RELOCO_OUT, bool c) noexcept {
  if (c)
    *x = 1;
}  // expect: reloco-out-param
void early_return(int &x RELOCO_OUT, bool c) noexcept {
  if (c)
    return;  // expect: reloco-out-param
  x = 1;
}
void never(int *x RELOCO_OUT) noexcept {}  // expect: reloco-out-param
int read_first(int *x RELOCO_OUT) noexcept {
  int v = *x;  // expect: reloco-out-param
  *x = v;
  return v;
}
void bad_const(const int *x RELOCO_OUT) noexcept { (void)x; }  // expect: reloco-out-param
