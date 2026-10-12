// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-no-raw-pointer
#include <reloco/lifetime.hpp>

struct holder {
  int *p;  // expect: reloco-no-raw-pointer
  void (*fn)() noexcept;
  const struct my_vtable *vt;
};
struct RELOCO_POINTER handle { int *p; };
struct RELOCO_OWNER owner { int *p; };

int *take(int *p) noexcept;  // expect: reloco-no-raw-pointer, reloco-no-raw-pointer
RELOCO_UNSAFE_BUFFER_USAGE int *unsafe_take(int *p) noexcept;
extern "C" int c_api(const char *s) noexcept;
void local() noexcept {
  int x = 0;
  int *p = &x;  // expect: reloco-no-raw-pointer
  (void)p;
}
