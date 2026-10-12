// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-uninit-local
struct pod {
  int a;
};
struct inited {
  int a = 0;
  inited() noexcept {}
};

void f() noexcept {
  int a;  // expect: reloco-uninit-local
  int arr[4];  // expect: reloco-uninit-local
  pod p;  // expect: reloco-uninit-local
  int ok1 = 0;
  int ok2{};
  int ok3[4] = {};
  pod ok4{};
  inited ok5;
  static int ok6;
  int nl;  // NOLINT(reloco-uninit-local)
  (void)a; (void)arr; (void)p; (void)ok1; (void)ok2; (void)ok3; (void)ok4; (void)ok5; (void)nl;
}

template <typename R> void range_for_in_template(R &r) noexcept {
  for (int child : r) { // no diagnostic: initialised by the loop
    (void)child;
  }
}
