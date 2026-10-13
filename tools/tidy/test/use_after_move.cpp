// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-use-after-move
#include <string>
#include <utility>

void sink(std::string s) noexcept;

void ok_no_reuse() noexcept {
  std::string s;
  sink(std::move(s));
}
void ok_reassigned() noexcept {
  std::string s;
  sink(std::move(s));
  s = "again";
  sink(std::move(s));
}
void ok_cleared() noexcept {
  std::string s;
  sink(std::move(s));
  s.clear();
  sink(s);
}
void ok_scalar(int x) noexcept {
  int y = std::move(x);
  y += x;
}
void ok_branches(bool c) noexcept {
  std::string s;
  if (c)
    sink(std::move(s));
  else
    sink(s);
}

void bad_simple() noexcept {
  std::string s;
  sink(std::move(s));
  sink(s);  // expect: reloco-use-after-move
}
void bad_branch(bool c) noexcept {
  std::string s;
  if (c)
    sink(std::move(s));
  sink(s);  // expect: reloco-use-after-move
}
void bad_loop(int n) noexcept {
  std::string s;
  for (int i = 0; i < n; ++i)
    sink(std::move(s));  // expect: reloco-use-after-move
}
void bad_member_call() noexcept {
  std::string s;
  sink(std::move(s));
  (void)s.size();  // expect: reloco-use-after-move
}
void suppressed() noexcept {
  std::string s;
  sink(std::move(s));
  sink(s);  // NOLINT(reloco-use-after-move)
}

// --- partial moves: a moved field poisons that field and the whole object, not its siblings ----------
struct pair_of {
  std::string first;
  std::string second;
  int count = 0;
};
void take(const pair_of &) noexcept;

void bad_field_reused() noexcept {
  pair_of p;
  sink(std::move(p.first));
  (void)p.first.size();  // expect: reloco-use-after-move
}
void bad_whole_after_field() noexcept {
  pair_of p;
  sink(std::move(p.first));
  take(p);  // expect: reloco-use-after-move
}
void bad_nested_path() noexcept {
  struct outer { pair_of in; } o;
  sink(std::move(o.in.first));
  (void)o.in.first.size();  // expect: reloco-use-after-move
}
void bad_parent_of_moved() noexcept {
  struct outer { pair_of in; } o;
  sink(std::move(o.in.first));
  take(o.in);  // expect: reloco-use-after-move
}
void ok_sibling_field() noexcept {
  pair_of p;
  sink(std::move(p.first));
  (void)p.second.size();
  p.count = 1;
}
void ok_field_reassigned() noexcept {
  pair_of p;
  sink(std::move(p.first));
  p.first = "again";
  take(p);
}
void ok_field_cleared() noexcept {
  pair_of p;
  sink(std::move(p.first));
  p.first.clear();
  take(p);
}
void ok_whole_reassigned() noexcept {
  pair_of p;
  sink(std::move(p.first));
  p = pair_of{};
  take(p);
}
void ok_scalar_field() noexcept {
  pair_of p;
  int c = std::move(p.count);
  (void)p.count;
  (void)c;
}

// --- members of `this` ---------------------------------------------------------------------------
class holder {
  std::string name_;
  std::string label_;

public:
  void bad_member_reused() noexcept {
    sink(std::move(name_));
    (void)name_.size();  // expect: reloco-use-after-move
  }
  void bad_this_arrow() noexcept {
    sink(std::move(this->name_));
    (void)this->name_.size();  // expect: reloco-use-after-move
  }
  void ok_other_member() noexcept {
    sink(std::move(name_));
    (void)label_.size();
  }
  void ok_member_reassigned() noexcept {
    sink(std::move(name_));
    name_ = "x";
    (void)name_.size();
  }
  holder &ok_bare_this_after_move() noexcept {
    sink(std::move(name_));
    return *this;  // a bare 'this' is not a use of the moved member
  }
};

// --- a variable declared in a loop body is a new object each iteration --------------------------------
void ok_declared_per_iteration() noexcept {
  for (int i = 0; i < 3; ++i) {
    std::string s = "x";
    sink(std::move(s));
  }
}
void bad_declared_outside_loop() noexcept {
  std::string s = "x";
  for (int i = 0; i < 3; ++i)
    sink(std::move(s));  // expect: reloco-use-after-move
}

// static_cast<T &&> and std::forward<T> with a value type are spelled-out moves.
void bad_static_cast() noexcept {
  std::string s;
  sink(static_cast<std::string &&>(s));
  (void)s.size();  // expect: reloco-use-after-move
}
template <typename T> void fwd_value(T &&) noexcept;
void bad_forward_value() noexcept {
  std::string s;
  fwd_value(std::forward<std::string>(s));
  (void)s.size();  // expect: reloco-use-after-move
}
void ok_forward_lvalue() noexcept {
  std::string s;
  fwd_value(std::forward<std::string &>(s));
  (void)s.size();
}
