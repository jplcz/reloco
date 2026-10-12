// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-virtual-call-in-ctor
struct base {
  base() noexcept { setup(); } // expect: reloco-virtual-call-in-ctor
  virtual ~base() noexcept { teardown(); } // expect: reloco-virtual-call-in-ctor
  virtual void setup() noexcept {}
  virtual void teardown() noexcept {}
  void helper() noexcept { setup(); } // fine: not a constructor
};

struct explicit_binding {
  explicit_binding() noexcept { explicit_binding::setup(); } // fine: qualified, statically bound
  virtual ~explicit_binding() = default;
  virtual void setup() noexcept {}
};

struct final_class final : base {
  final_class() noexcept { teardown(); } // fine: nothing can derive, so the call reaches the right override
};

struct final_method {
  final_method() noexcept { done(); } // fine: cannot be overridden
  virtual ~final_method() = default;
  virtual void done() noexcept final {}
};
