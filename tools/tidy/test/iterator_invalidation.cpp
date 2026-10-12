// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-iterator-invalidation
#include <vector>

void grow(std::vector<int> &v) noexcept;

struct holder {
  std::vector<int> items;
  void bad_member() noexcept {
    for (int x : items)
      items.push_back(x);  // expect: reloco-iterator-invalidation
  }
};

void ok_read_only(std::vector<int> &v) noexcept {
  int sum = 0;
  for (int x : v)
    sum += x;
  v.push_back(sum);
}
void ok_element_write(std::vector<int> &v) noexcept {
  for (int &x : v)
    x += 1;
  for (std::size_t i = 0; i < v.size(); ++i)
    v.push_back(v[i]);
}
void ok_other_container(std::vector<int> &v, std::vector<int> &w) noexcept {
  for (int x : v)
    w.push_back(x);
}
void ok_accessors(std::vector<int> &v) noexcept {
  for (int x : v)
    if (v.size() > 3 && v.front() == x)
      (void)v.at(0);
}
void ok_stops_after_change(std::vector<int> &v) noexcept {
  for (int x : v) {
    if (x == 1) {
      v.push_back(2);
      break;
    }
  }
}

void bad_push(std::vector<int> &v) noexcept {
  for (int x : v)
    v.push_back(x);  // expect: reloco-iterator-invalidation
}
void bad_clear(std::vector<int> &v) noexcept {
  for (int x : v) {
    if (x == 0)
      v.clear();  // expect: reloco-iterator-invalidation
  }
}
void bad_handoff(std::vector<int> &v) noexcept {
  for (int x : v) {
    (void)x;
    grow(v);  // expect: reloco-iterator-invalidation
  }
}
void suppressed(std::vector<int> &v) noexcept {
  for (int x : v)
    v.push_back(x);  // NOLINT(reloco-iterator-invalidation)
}
