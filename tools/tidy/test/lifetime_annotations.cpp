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

// A handle constructor only borrows a parameter that reaches an aliasing member.
struct cfg_t {
  int x = 0;
};
struct RELOCO_POINTER copies_cfg {
  explicit copies_cfg(const cfg_t &c) noexcept : cfg_(c) {} // copied into a value member: nothing is borrowed
  explicit copies_cfg(const cfg_t &c, int) noexcept { cfg_ = c; }
  cfg_t cfg_;
  const int *q = nullptr;
};
struct RELOCO_POINTER assigns_in_body {
  explicit assigns_in_body(int &r) noexcept { p = &r; } // expect: reloco-lifetime-annotations
  int *p = nullptr;
};
struct RELOCO_POINTER delegates {
  delegates(RELOCO_LIFETIMEBOUND int &r, int) noexcept : p(&r) {}
  explicit delegates(int &r) noexcept : delegates(r, 0) {} // expect: reloco-lifetime-annotations
  int *p = nullptr;
};

// RELOCO_COPIES states that the parameter is only read; a scalar read from the pointee is a copy anyway.
struct RELOCO_POINTER declared_copy {
  declared_copy(const cfg_t &c RELOCO_COPIES) noexcept;
  declared_copy(int &r RELOCO_COPIES, int) noexcept;
  declared_copy(int &r) noexcept; // expect: reloco-lifetime-annotations
};
struct RELOCO_POINTER scalar_from_pointee {
  scalar_from_pointee(int &r, const cfg_t &c) noexcept : p(&r), n(c.x) {} // expect: reloco-lifetime-annotations
  int *p = nullptr;
  int n = 0;
};
