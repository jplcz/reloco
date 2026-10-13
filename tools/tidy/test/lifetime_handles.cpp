// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-lifetime-names,reloco-borrow-check
#include <reloco/lifetime.hpp>

// A handle class: the labelled field makes it hold a borrow, so constructing it borrows its labelled arguments.
class holder {
public:
  explicit holder(int *p RELOCO_LT(a)) noexcept : p_(p) {}
  RELOCO_LT_RETURN(a) int *get() const & noexcept { return p_; }
  void reset(int *p RELOCO_LT(a)) & noexcept { p_ = p; }

private:
  RELOCO_LT(a) int *p_;
};

int ok_handle_unrelated(int x, int y) noexcept {
  holder h(&x);
  y = 1;
  return *h.get() + y;
}
int ok_handle_dead(int x) noexcept {
  holder h(&x);
  const int v = *h.get();
  x = 2;  // h is not used again
  return v;
}
int bad_handle_ctor(int x) noexcept {
  holder h(&x);
  x = 2;  // expect: reloco-borrow-check
  return *h.get();
}
int bad_handle_copy(int x) noexcept {
  holder h(&x);
  holder h2 = h;
  x = 3;  // expect: reloco-borrow-check
  return *h2.get();
}
int bad_accessor_result(int x) noexcept {
  holder h(&x);
  int *p = h.get();  // p inherits the borrow of x, not of h
  x = 4;  // expect: reloco-borrow-check
  return *p;
}
int ok_accessor_result_outlives_handle(int x) noexcept {
  int *p = nullptr;
  {
    holder h(&x);
    p = h.get();
  }
  return *p;
}
int ok_reset_while_pointer_live(int x, int y) noexcept {
  holder h(&x);
  int *p = h.get();
  h.reset(&y);  // get() does not borrow h itself
  return *p;
}

// A view derived from a view keeps the original storage borrowed.
struct buffer {
  int v[4] = {};
  int *data() & noexcept RELOCO_LIFETIMEBOUND { return v; }
};
int bad_pointer_copy(buffer &b) noexcept {
  int *p = b.data();
  int *q = p;
  b.v[0] = 1;  // expect: reloco-borrow-check
  return *q;
}
int ok_pointer_copy_dead(buffer &b) noexcept {
  int *p = b.data();
  int *q = p;
  const int r = *q;
  b.v[0] = 1;
  return r;
}

// Every constructor must initialise a labelled field.
class partial {
public:
  explicit partial(int *p RELOCO_LT(a)) noexcept : p_(p) {}
  partial() noexcept {}  // expect: reloco-lifetime-names
  partial(int, int) noexcept : partial(nullptr) {}

private:
  RELOCO_LT(a) int *p_;
};
class defaulted {
public:
  defaulted() noexcept {}

private:
  RELOCO_LT(a) int *p_ = nullptr;
};
// Uninitialised storage is valid here: the class opts out with a reason.
// A pool slot is filled in later by emplace(), before any read.
class RELOCO_BORROW_EXEMPT slot {
public:
  slot() noexcept {}

private:
  RELOCO_LT(a) int *p_;
};
class exempt_field {
public:
  exempt_field() noexcept {}

private:
  RELOCO_BORROW_EXEMPT RELOCO_LT(a) int *p_;
};
class exempt_ctor {
public:
  RELOCO_BORROW_EXEMPT exempt_ctor() noexcept {}

private:
  RELOCO_LT(a) int *p_;
};
