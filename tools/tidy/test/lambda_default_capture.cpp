// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// checks: reloco-lambda-default-capture
void run() noexcept {
  int a = 1, b = 2;
  auto by_copy = [=]() noexcept { return a + b; }; // expect: reloco-lambda-default-capture
  auto by_ref = [&]() noexcept { return a + b; }; // expect: reloco-lambda-default-capture
  auto mixed = [&, a]() noexcept { return a + b; }; // expect: reloco-lambda-default-capture
  auto explicit_caps = [a, &b]() noexcept { return a + b; };
  auto none = []() noexcept { return 1; };
  (void)by_copy; (void)by_ref; (void)mixed; (void)explicit_caps; (void)none;
}

struct widget {
  int v = 0;
  int run(int k) noexcept {
    auto f = [&]() noexcept { return v + k; };  // expect: reloco-lambda-default-capture
    return f();
  }
};
