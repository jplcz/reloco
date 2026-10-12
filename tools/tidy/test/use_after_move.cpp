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
