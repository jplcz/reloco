// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-member-order
#include <reloco/lifetime.hpp>

struct mutex {
  int state = 0;
};
struct guard {
  explicit guard(mutex &m) noexcept : m_(&m) {}
  ~guard() { m_->state = 0; }
  mutex *m_;
};

// Guard declared before the mutex it locks: constructed first, destroyed last.
struct bad_order {
  guard g_;
  mutex mu_;
  bad_order() noexcept : g_(mu_) {} // expect: reloco-member-order
  bad_order(const bad_order &) = delete;
  bad_order &operator=(const bad_order &) = delete;
};

struct good_order {
  mutex mu_;
  guard g_;
  good_order() noexcept : g_(mu_) {}
  good_order(const good_order &) = delete;
  good_order &operator=(const good_order &) = delete;
};

// Pointer into own buffer, class still copyable: dangling after copy.
struct self_ref {
  char buf_[16] = {};
  char *cur_ = buf_; // expect: reloco-member-order
};

struct self_ref_deleted {
  char buf_[16] = {};
  char *cur_ = buf_;
  self_ref_deleted() = default;
  self_ref_deleted(const self_ref_deleted &) = delete;
  self_ref_deleted &operator=(const self_ref_deleted &) = delete;
};

struct other_ptr {
  int *p_ = nullptr;
  explicit other_ptr(int &x) noexcept : p_(&x) {}
};
