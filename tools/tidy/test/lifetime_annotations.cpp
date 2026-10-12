// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-lifetime-annotations
#include <reloco/lifetime.hpp>

struct buf {
  int v;
  int &get() & noexcept { return v; }  // expect: reloco-lifetime-annotations
  int &get_ok() & noexcept RELOCO_LIFETIMEBOUND { return v; }
  buf &self() & noexcept { return *this; }
  int value() const noexcept { return v; }
};
struct RELOCO_POINTER view {
  view(int &r) noexcept : p(&r) {}  // expect: reloco-lifetime-annotations
  view(RELOCO_LIFETIMEBOUND int &r, int) noexcept : p(&r) {}
  int *p;
  int &deref() const noexcept { return *p; }  // handles hand out their pointee
};

struct fixit_shapes {
  int v = 0;
  int &a() & noexcept { return v; }                       // expect: reloco-lifetime-annotations
  const int &b() const & noexcept(true) { return v; }     // expect: reloco-lifetime-annotations
  int &c() & { return v; }                                // expect: reloco-lifetime-annotations
  auto d() & noexcept -> int & { return v; }              // expect: reloco-lifetime-annotations
};
