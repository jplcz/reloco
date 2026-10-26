// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// Test-only audit helper: runs an optimized kernel and its scalar reference over copies of the same inputs and checks
// that the results (value or error) and the output buffers are identical. Not part of the production headers.

#include <gtest/gtest.h>
#include <reloco/error.hpp>

#include <vector>

namespace audit {

// Compares two `result<T>` values: both succeed with equal values, or both fail with the same error.
template <class R> void expect_same_result(const R &optimized, const R &reference) {
  ASSERT_EQ(optimized.has_value(), reference.has_value());
  if (!optimized.has_value()) {
    EXPECT_EQ(optimized.error(), reference.error());
    return;
  }
  if constexpr (!std::is_void_v<typename R::value_type>)
    EXPECT_EQ(optimized.value(), reference.value());
}

// Runs `kernel(out)` on two copies of `initial_out`, once with each variant, and compares results and buffers.
// `optimized` and `reference` take a `std::vector<T> &` output buffer and return a result.
template <class T, class Opt, class Ref>
void expect_same(const std::vector<T> &initial_out, Opt optimized, Ref reference) {
  std::vector<T> out_opt = initial_out;
  std::vector<T> out_ref = initial_out;
  auto r_opt = optimized(out_opt);
  auto r_ref = reference(out_ref);
  expect_same_result(r_opt, r_ref);
  if (r_opt.has_value())
    EXPECT_EQ(out_opt, out_ref); // after an error the output is unspecified
}

} // namespace audit
