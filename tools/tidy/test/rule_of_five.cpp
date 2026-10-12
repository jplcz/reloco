// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-rule-of-five
#include <reloco/lifetime.hpp>

struct partial {  // expect: reloco-rule-of-five
  partial(const partial &) noexcept;
};
struct base {
  virtual ~base() = default;
};
struct full {
  full(const full &) = delete;
  full &operator=(const full &) = delete;
  full(full &&) noexcept = default;
  full &operator=(full &&) noexcept = default;
  ~full() = default;
};
struct none { int x = 0; };
// Both copy operations deleted: satisfied without spelling out the moves.
struct pinned {
  pinned(const pinned &) = delete;
  pinned &operator=(const pinned &) = delete;
  ~pinned() = default;
};
