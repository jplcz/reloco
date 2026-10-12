// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-early-static
#include <reloco/lifetime.hpp>

int runtime_value() noexcept;

struct RELOCO_EARLY_STATIC good {
  int a;
  constexpr good() noexcept : a(1) {}
  constexpr explicit good(int v) noexcept : a(v) {}
};

struct RELOCO_EARLY_STATIC non_constexpr_ctor {
  int a;
  non_constexpr_ctor() noexcept : a(runtime_value()) {}  // expect: reloco-early-static
};

struct RELOCO_EARLY_STATIC has_dtor {
  int a = 0;
  ~has_dtor() noexcept {}  // expect: reloco-early-static
};

struct plain_dtor {
  ~plain_dtor() noexcept {}
};

constinit good ok_constinit;
constexpr good ok_constexpr{5};
good ok_const_args{3};
good ok_zero_init;

constinit good bad_dynamic{runtime_value()};  // expect: reloco-early-static

struct RELOCO_EARLY_STATIC suppressed_type {
  suppressed_type() noexcept {}  // NOLINT(reloco-early-static)
};

plain_dtor not_marked{};
