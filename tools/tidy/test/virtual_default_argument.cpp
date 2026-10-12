// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-virtual-default-argument
struct widget {
  virtual ~widget() = default;
  virtual int size(int scale = 1) const noexcept; // expect: reloco-virtual-default-argument
  virtual int count(int n) const noexcept;
  int helper(int scale = 1) const noexcept { return scale; }
  int forward() const noexcept { return count(1); }
};
