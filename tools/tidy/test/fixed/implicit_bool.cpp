// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-implicit-bool
struct flag_holder {
  explicit operator bool() const noexcept { return true; }
};

void run(int n, const int *p, double d, bool b, flag_holder h) noexcept {
  if (n != 0) {} // expect: reloco-implicit-bool
  if (p != nullptr) {} // expect: reloco-implicit-bool
  while (d != 0.0) {} // expect: reloco-implicit-bool
  bool x = !n; // expect: reloco-implicit-bool
  if (n != 0) {}
  if (p != nullptr) {}
  if (b) {}
  if (h) {}
  (void)x;
}

// A condition variable has no separate operand to compare: reported, but without a fix-it.
int *make_ptr() noexcept;
int use_cond_var() noexcept {
  if (int *p = make_ptr()) {  // expect: reloco-implicit-bool
    return *p;
  }
  return 0;
}
