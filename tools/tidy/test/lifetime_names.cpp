// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-lifetime-names,reloco-borrow-check
#include <reloco/lifetime.hpp>

struct item {
  int v = 0;
};

// The result borrows x and y (lifetime a), never z.
RELOCO_LT_RETURN(a) const item &pick(const item &x RELOCO_LT(a), const item &y RELOCO_LT(a), const item &z) noexcept;

int uses_pick_ok(item &p, item &q) noexcept {
  item other;
  const item &r = pick(p, other, q);  // borrows p and other, not q
  q.v = 1;
  return r.v;
}
int mutate_first_borrow(item &p, item &q) noexcept {
  const item &r = pick(p, q, p);
  q.v = 1;  // expect: reloco-borrow-check
  return r.v;
}
int mutate_second_borrow(item &p, item &q) noexcept {
  const item &r = pick(p, q, p);
  p.v = 1;  // expect: reloco-borrow-check
  return r.v;
}

// Returning a parameter that does not carry the returned lifetime.
RELOCO_LT_RETURN(a) const item &first_of(const item &x RELOCO_LT(a), const item &y) noexcept {
  (void)y;
  return x;
}
RELOCO_LT_RETURN(a) const item &wrong_one(const item &x RELOCO_LT(a), const item &y) noexcept {
  (void)x;
  return y;  // expect: reloco-lifetime-names
}

// A lifetime nobody carries / a label on something that cannot borrow.
RELOCO_LT_RETURN(b) const item &nobody(const item &x) noexcept;  // expect: reloco-lifetime-names
RELOCO_LT_RETURN(a) int not_a_view(const item &x RELOCO_LT(a)) noexcept;  // expect: reloco-lifetime-names
void by_value(int n RELOCO_LT(a)) noexcept;  // expect: reloco-lifetime-names

// Receiver lifetimes.
struct holder {
  item it;
  item other;
  RELOCO_LT_THIS(a) RELOCO_LT_RETURN(a) const item &get() const & noexcept { return it; }
  RELOCO_LT_RETURN(a) const item &leak() const & noexcept { return it; }  // expect: reloco-lifetime-names
  void set(int n) & noexcept { it.v = n; }
};
int receiver_borrow(holder &h) noexcept {
  const item &r = h.get();
  h.set(1);  // expect: reloco-borrow-check
  return r.v;
}

// 'static requirements.
void register_name(const char *name RELOCO_LT(static)) noexcept;
RELOCO_LT_RETURN(static) const char *name_of(int k) noexcept {
  static const char names[] = "ab";
  (void)k;
  return names;
}
RELOCO_LT_RETURN(static) const char *bad_name(const char *s) noexcept {
  return s;  // expect: reloco-lifetime-names
}
RELOCO_LT_RETURN(static) const char *good_name(const char *s RELOCO_LT(static)) noexcept { return s; }

static const char g_name[] = "global";
void register_all(const char *param, const char *fwd RELOCO_LT(static)) noexcept {
  register_name("literal");
  register_name(g_name);
  register_name(name_of(1));
  register_name(fwd);
  register_name(param);  // expect: reloco-lifetime-names
  char local[4] = {};
  register_name(local);  // expect: reloco-lifetime-names
  register_name(&local[1]);  // expect: reloco-lifetime-names
}

// --- Fields and variables ---------------------------------------------------------------------------------

// A view that keeps a pointer tagged with lifetime 'a'. The constructor must be given something tied to 'a'.
struct tagged_view {
  RELOCO_LT(a) const item *it_ = nullptr;
  RELOCO_LT(static) const char *label_ = nullptr;
  RELOCO_LT(a) int not_a_pointer_ = 0;  // expect: reloco-lifetime-names

  tagged_view(const item *it RELOCO_LT(a), const char *label RELOCO_LT(static)) noexcept : it_(it), label_(label) {}
  tagged_view(const item *it, int) noexcept : it_(it) {}  // expect: reloco-lifetime-names
  tagged_view(const item *it RELOCO_LT(b)) noexcept : it_(it) {}  // expect: reloco-lifetime-names
  tagged_view(const char *text) noexcept : label_(text) {}  // expect: reloco-lifetime-names

  void rebind(const item *it RELOCO_LT(a)) & noexcept { it_ = it; }
  void bad_rebind(const item *it) & noexcept {
    it_ = it;  // expect: reloco-lifetime-names
  }
  void relabel(const char *text RELOCO_LT(static)) & noexcept { label_ = text; }
  void bad_relabel(const char *text) & noexcept {
    label_ = text;  // expect: reloco-lifetime-names
  }
  void local_into_field() & noexcept {
    item tmp;
    it_ = &tmp;  // expect: reloco-lifetime-names
  }
  void static_into_field() & noexcept {
    label_ = "literal";
    label_ = g_name;
  }

  // A field tagged with the returned lifetime hands it out without naming a receiver lifetime.
  RELOCO_LT_RETURN(a) const item *get() const & noexcept { return it_; }
  RELOCO_LT_RETURN(static) const char *label() const & noexcept { return label_; }
  RELOCO_LT_RETURN(b) const item *wrong() const & noexcept { return it_; }  // expect: reloco-lifetime-names
};

// In-class initialisers and variables with 'static lifetime.
struct defaults {
  RELOCO_LT(static) const char *ok = "literal";
  RELOCO_LT(static) const char *bad = nullptr;
};
RELOCO_LT(static) const char *g_label = "global";
void local_vars(const char *param) noexcept {
  RELOCO_LT(static) const char *a = "literal";
  RELOCO_LT(static) const char *b = param;  // expect: reloco-lifetime-names
  char buf[4] = {};
  RELOCO_LT(static) const char *c = buf;  // expect: reloco-lifetime-names
  (void)a; (void)b; (void)c;
}
