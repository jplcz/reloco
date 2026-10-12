// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-slicing
struct shape {
  shape() = default;
  shape(const shape &) = default;
  virtual ~shape() = default;
  virtual int area() const noexcept { return 0; }
};
struct square : shape {
  int side = 2;
  int area() const noexcept override { return side * side; }
};
struct plain_base {
  int a = 0;
};
struct plain_derived : plain_base {
  int b = 0;
};

int by_value(shape s) noexcept { return s.area(); }
int by_ref(const shape &s) noexcept { return s.area(); }

void run() noexcept {
  square sq;
  shape copy = sq; // expect: reloco-slicing
  by_value(sq);    // expect: reloco-slicing
  by_ref(sq);
  shape same = copy;
  square sq2 = sq;
  plain_derived pd;
  plain_base pb = pd; // fine: not polymorphic
  (void)same; (void)sq2; (void)pb;
}
