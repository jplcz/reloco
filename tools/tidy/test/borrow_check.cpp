// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-borrow-check
#include <reloco/lifetime.hpp>

struct buf {
  int a = 0;
  int b = 0;
  int v[4] = {};
  int &first() & noexcept RELOCO_LIFETIMEBOUND { return v[0]; }
  const int &cfirst() const & noexcept RELOCO_LIFETIMEBOUND { return v[0]; }
  void push(int x) & noexcept { a = x; }
  int size() const noexcept { return a; }
};
void take(buf &) noexcept;
void take_const(const buf &) noexcept;

int shared_then_mutate() noexcept {
  buf x;
  const int &r = x.cfirst();
  x.push(1);  // expect: reloco-borrow-check
  return r;
}
int shared_then_read_ok() noexcept {
  buf x;
  const int &r = x.cfirst();
  int s = x.size();
  take_const(x);
  return r + s;
}
int nll_borrow_ended_ok() noexcept {
  buf x;
  const int &r = x.cfirst();
  int copy = r;
  x.push(copy);  // r is no longer used: fine
  return copy;
}
int mutable_then_read() noexcept {
  buf x;
  int &m = x.first();
  int s = x.size();  // expect: reloco-borrow-check
  m = s;
  return m;
}
int two_mutable() noexcept {
  int x = 1;
  int &m = x;
  int &n = x;  // expect: reloco-borrow-check
  m = 2;
  return n;
}
int shared_then_mut_borrow() noexcept {
  buf x;
  const int &r = x.cfirst();
  take(x);  // expect: reloco-borrow-check
  return r;
}
int disjoint_fields_ok() noexcept {
  buf x;
  int &ra = x.a;
  int &rb = x.b;
  ra = 1;
  rb = 2;
  return ra + rb;
}
int same_field() noexcept {
  buf x;
  int &ra = x.a;
  x.a = 3;  // expect: reloco-borrow-check
  return ra;
}
int loop_extends_borrow() noexcept {
  buf x;
  const int &r = x.cfirst();
  int s = 0;
  for (int i = 0; i < 3; ++i) {
    s += r;
    x.push(i);  // expect: reloco-borrow-check
  }
  return s;
}
void range_for_invalidation() noexcept {
  buf x;
  buf *unused = nullptr;
  (void)unused;
  for (int &e : x.v) {
    e = 1;
    x.push(e);  // expect: reloco-borrow-check
  }
}
void range_for_const_ok() noexcept {
  buf x;
  for (int e : x.v)
    take_const(x);
}

// --- borrows of members through `this` --------------------------------------
struct holder {
  int a_ = 0;
  int b_ = 0;
  int v_[4] = {};
  int &first() & noexcept RELOCO_LIFETIMEBOUND { return v_[0]; }
  void push(int x) & noexcept { a_ = x; }
  int get() const & noexcept { return a_; }

  int member_shared_then_mutate() & noexcept {
    const int &r = a_;
    a_ = 5;  // expect: reloco-borrow-check
    return r;
  }
  int member_shared_then_method() & noexcept {
    const int &r = a_;
    push(1);  // expect: reloco-borrow-check
    return r;
  }
  int member_via_arrow() & noexcept {
    const int &r = this->a_;
    this->a_ = 2;  // expect: reloco-borrow-check
    return r;
  }
  int member_method_result() & noexcept {
    int &f = first();
    push(3);  // expect: reloco-borrow-check
    return f;
  }
  int member_nll_ok() & noexcept {
    const int &r = a_;
    int copy = r;
    a_ = copy + 1;  // r is dead: fine
    return copy;
  }
  int member_disjoint_ok() & noexcept {
    int &ra = a_;
    b_ = 1;
    return ra;
  }
  int member_read_ok() & noexcept {
    const int &r = a_;
    return r + get() + b_;
  }
  holder *self_view_ok() & noexcept {
    holder *self = this;
    a_ = 1;
    return self;
  }
};

// --- exemptions -----------------------------------------------------------
RELOCO_BORROW_EXEMPT void swap_like(buf &a, buf &b) noexcept {
  int &ra = a.a;
  int &rb = b.a;
  int t = ra;
  ra = rb;
  rb = t;
  a.push(1);
  (void)rb;
}
int exempt_by_unsafe_code() noexcept {
  buf x;
  const int &r = x.cfirst();
  RELOCO_BEGIN_UNSAFE_CODE
  x.push(1);
  RELOCO_END_UNSAFE_CODE
  return r;
}
int exempt_by_unsafe_buffer_block() noexcept {
  buf x;
  const int &r = x.cfirst();
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  x.push(1);
  RELOCO_END_UNSAFE_BUFFER_USAGE
  return r;
}
int exempt_by_nolint() noexcept {
  buf x;
  const int &r = x.cfirst();
  x.push(1);  // NOLINT(reloco-borrow-check): r is re-read only after the push in this fixture
  return r;
}
RELOCO_UNSAFE_BUFFER_USAGE int unsafe_tier() noexcept {
  buf x;
  const int &r = x.cfirst();
  x.push(1);
  return r;
}

// --- value copies are not borrows ------------------------------------------
struct node_holder {
  int *head = nullptr;
  void swap_head() & noexcept {
    int *old = head; // copies the pointer value, it does not borrow 'head'
    head = nullptr;
    (void)old;
  }
};
