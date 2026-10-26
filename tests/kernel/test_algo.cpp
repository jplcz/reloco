// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "audit.hpp"

#include <gtest/gtest.h>
#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

namespace kernel = reloco::kernel;
using reloco::span;

std::vector<int> random_vector(std::mt19937 &rng, std::size_t n) {
  std::vector<int> v(n);
  for (auto &x : v)
    x = static_cast<int>(rng() % 201) - 100;
  return v;
}

TEST(KernelAlgoTest, FillSetsEveryElement) {
  std::vector<int> v(5, 1);
  kernel::fill(span<int>(v), 7);
  EXPECT_EQ(v, std::vector<int>(5, 7));
  std::vector<int> empty;
  kernel::fill(span<int>(empty), 7);
}

TEST(KernelAlgoTest, CopyChecksLengthAndOverlap) {
  std::vector<int> in{1, 2, 3};
  std::vector<int> out(4, 0);
  ASSERT_TRUE(kernel::try_copy(span<int>(out), span<const int>(in)).has_value());
  EXPECT_EQ(out, (std::vector<int>{1, 2, 3, 0}));

  std::vector<int> small(2);
  EXPECT_EQ(kernel::try_copy(span<int>(small), span<const int>(in)).error(), reloco::error::out_of_bounds);

  std::vector<int> buf{1, 2, 3, 4};
  auto r = kernel::try_copy(span<int>(buf).subspan(1, 3), span<const int>(buf).subspan(0, 3));
  EXPECT_EQ(r.error(), reloco::error::invalid_argument);
}

TEST(KernelAlgoTest, TransformMatchesScalarReference) {
  std::mt19937 rng(1);
  for (std::size_t n : {0U, 1U, 3U, 4U, 17U, 64U}) {
    auto in = random_vector(rng, n);
    std::vector<int> out(n), ref(n);
    for (std::size_t i = 0; i < n; ++i)
      ref[i] = in[i] * 3 + 1;
    ASSERT_TRUE(
        kernel::try_transform(span<int>(out), span<const int>(in), [](int x) { return x * 3 + 1; }).has_value());
    EXPECT_EQ(out, ref);
  }
}

TEST(KernelAlgoTest, TransformInPlace) {
  std::vector<int> v{1, 2, 3};
  kernel::transform_in_place(span<int>(v), [](int x) { return x + 10; });
  EXPECT_EQ(v, (std::vector<int>{11, 12, 13}));
}

TEST(KernelAlgoTest, ZipTransform) {
  std::vector<int> a{1, 2, 3}, b{10, 20, 30}, out(3);
  ASSERT_TRUE(kernel::try_zip_transform(span<int>(out), span<const int>(a), span<const int>(b), [](int x, int y) {
                return x + y;
              }).has_value());
  EXPECT_EQ(out, (std::vector<int>{11, 22, 33}));
  std::vector<int> shorter{1};
  EXPECT_EQ(kernel::try_zip_transform(span<int>(out), span<const int>(a), span<const int>(shorter),
                                      [](int x, int y) { return x + y; })
                .error(),
            reloco::error::invalid_argument);
}

TEST(KernelAlgoTest, ReductionsMatchScalarReference) {
  std::mt19937 rng(2);
  for (int iter = 0; iter < 300; ++iter) {
    auto v = random_vector(rng, 1 + rng() % 40);
    std::int64_t sum = 0;
    std::size_t amin = 0, amax = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
      sum += v[i];
      if (v[i] < v[amin])
        amin = i;
      if (v[amax] < v[i])
        amax = i;
    }
    span<const int> s(v);
    EXPECT_EQ(kernel::try_sum<std::int64_t>(s).value(), sum);
    EXPECT_EQ(kernel::try_argmin(s).value(), amin);
    EXPECT_EQ(kernel::try_argmax(s).value(), amax);
    EXPECT_EQ(kernel::try_min(s).value(), v[amin]);
    EXPECT_EQ(kernel::try_max(s).value(), v[amax]);
    EXPECT_EQ(kernel::try_dot<std::int64_t>(s, s).value(), [&] {
      std::int64_t d = 0;
      for (int x : v)
        d += static_cast<std::int64_t>(x) * x;
      return d;
    }());
  }
}

TEST(KernelAlgoTest, EmptyInputs) {
  std::vector<int> e;
  span<const int> s(e);
  EXPECT_EQ(kernel::try_sum<int>(s).value(), 0);
  EXPECT_EQ(kernel::try_min(s).error(), reloco::error::container_empty);
  EXPECT_EQ(kernel::try_argmax(s).error(), reloco::error::container_empty);
}

TEST(KernelAlgoTest, SumAndDotReportOverflowAndLengthMismatch) {
  std::vector<std::uint8_t> bytes{200, 100};
  EXPECT_EQ(kernel::try_sum<std::uint8_t>(span<const std::uint8_t>(bytes)).error(), reloco::error::integer_overflow);
  EXPECT_EQ(kernel::try_sum<int>(span<const std::uint8_t>(bytes)).value(), 300);
  std::vector<std::uint8_t> one{1};
  EXPECT_EQ(kernel::try_dot<int>(span<const std::uint8_t>(bytes), span<const std::uint8_t>(one)).error(),
            reloco::error::invalid_argument);
  EXPECT_EQ(kernel::try_dot<std::uint8_t>(span<const std::uint8_t>(bytes), span<const std::uint8_t>(bytes)).error(),
            reloco::error::integer_overflow);
}

TEST(KernelAlgoTest, ArgMinMaxReturnTheFirstOfTies) {
  std::vector<int> v{3, 1, 1, 5, 5};
  EXPECT_EQ(kernel::try_argmin(span<const int>(v)).value(), 1U);
  EXPECT_EQ(kernel::try_argmax(span<const int>(v)).value(), 3U);
}

TEST(KernelAlgoTest, ReduceWithCustomOp) {
  std::vector<int> v{1, 2, 3, 4};
  EXPECT_EQ(kernel::reduce(span<const int>(v), 1, [](int a, int x) { return a * x; }), 24);
}

TEST(KernelAlgoTest, ScansMatchScalarReference) {
  std::mt19937 rng(3);
  for (int iter = 0; iter < 200; ++iter) {
    auto v = random_vector(rng, rng() % 30);
    std::vector<std::int64_t> inc(v.size()), exc(v.size()), ref_inc(v.size()), ref_exc(v.size());
    std::int64_t run = 5;
    std::int64_t run_inc = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
      ref_exc[i] = run;
      run += v[i];
      run_inc += v[i];
      ref_inc[i] = run_inc;
    }
    ASSERT_TRUE(kernel::try_inclusive_scan(span<std::int64_t>(inc), span<const int>(v)).has_value());
    ASSERT_TRUE(kernel::try_exclusive_scan(span<std::int64_t>(exc), span<const int>(v), std::int64_t{5}).has_value());
    EXPECT_EQ(inc, ref_inc);
    EXPECT_EQ(exc, ref_exc);
  }
}

TEST(KernelAlgoTest, ScanErrors) {
  std::vector<std::uint8_t> in{200, 100};
  std::vector<std::uint8_t> out(2);
  EXPECT_EQ(kernel::try_inclusive_scan(span<std::uint8_t>(out), span<const std::uint8_t>(in)).error(),
            reloco::error::integer_overflow);
  // The sum after the last element is never stored, so the exclusive scan accepts it.
  EXPECT_TRUE(kernel::try_exclusive_scan(span<std::uint8_t>(out), span<const std::uint8_t>(in)).has_value());
  EXPECT_EQ(out[1], 200);
  std::vector<std::uint8_t> small(1);
  EXPECT_EQ(kernel::try_inclusive_scan(span<std::uint8_t>(small), span<const std::uint8_t>(in)).error(),
            reloco::error::out_of_bounds);
}

// Audits: every optimized algorithm against its fully checked scalar reference, over random lengths (including
// short outputs, which must produce the same error) and random values.
TEST(KernelAlgoAuditTest, ElementwiseAndScansMatchReference) {
  std::mt19937 rng(11);
  for (int iter = 0; iter < 400; ++iter) {
    auto in = random_vector(rng, rng() % 20);
    auto in2 = random_vector(rng, rng() % 3 == 0 ? rng() % 20 : in.size());
    const std::size_t out_n = rng() % 3 == 0 ? rng() % 20 : in.size() + rng() % 3;
    const std::vector<int> init(out_n, 9);
    span<const int> a(in);
    span<const int> b(in2);
    audit::expect_same(
        init, [&](std::vector<int> &o) { return kernel::try_copy(span<int>(o), a); },
        [&](std::vector<int> &o) { return kernel::reference::try_copy(span<int>(o), a); });
    audit::expect_same(
        init, [&](std::vector<int> &o) { return kernel::try_transform(span<int>(o), a, [](int x) { return x * 2; }); },
        [&](std::vector<int> &o) {
          return kernel::reference::try_transform(span<int>(o), a, [](int x) { return x * 2; });
        });
    audit::expect_same(
        init,
        [&](std::vector<int> &o) {
          return kernel::try_zip_transform(span<int>(o), a, b, [](int x, int y) { return x - y; });
        },
        [&](std::vector<int> &o) {
          return kernel::reference::try_zip_transform(span<int>(o), a, b, [](int x, int y) { return x - y; });
        });
    const std::vector<std::int8_t> small_init(out_n, 0);
    std::vector<std::int8_t> bytes(in.size());
    for (std::size_t i = 0; i < in.size(); ++i)
      bytes[i] = static_cast<std::int8_t>(in[i]);
    span<const std::int8_t> sb(bytes);
    // An int8 accumulator overflows on some inputs, so both variants must agree on the error as well.
    audit::expect_same(
        small_init, [&](std::vector<std::int8_t> &o) { return kernel::try_inclusive_scan(span<std::int8_t>(o), sb); },
        [&](std::vector<std::int8_t> &o) { return kernel::reference::try_inclusive_scan(span<std::int8_t>(o), sb); });
    audit::expect_same(
        small_init,
        [&](std::vector<std::int8_t> &o) {
          return kernel::try_exclusive_scan(span<std::int8_t>(o), sb, std::int8_t{3});
        },
        [&](std::vector<std::int8_t> &o) {
          return kernel::reference::try_exclusive_scan(span<std::int8_t>(o), sb, std::int8_t{3});
        });
  }
}

TEST(KernelAlgoAuditTest, ReductionsMatchReference) {
  std::mt19937 rng(12);
  for (int iter = 0; iter < 400; ++iter) {
    auto v = random_vector(rng, rng() % 20);
    auto w = random_vector(rng, rng() % 3 == 0 ? rng() % 20 : v.size());
    std::vector<std::int8_t> bytes(v.size());
    for (std::size_t i = 0; i < v.size(); ++i)
      bytes[i] = static_cast<std::int8_t>(v[i]);
    span<const int> a(v);
    span<const int> b(w);
    span<const std::int8_t> sb(bytes);
    audit::expect_same_result(kernel::try_sum<std::int8_t>(sb), kernel::reference::try_sum<std::int8_t>(sb));
    audit::expect_same_result(kernel::try_sum<std::int64_t>(a), kernel::reference::try_sum<std::int64_t>(a));
    audit::expect_same_result(kernel::try_dot<std::int16_t>(a, b), kernel::reference::try_dot<std::int16_t>(a, b));
    audit::expect_same_result(kernel::try_argmin(a), kernel::reference::try_argmin(a));
    audit::expect_same_result(kernel::try_argmax(a), kernel::reference::try_argmax(a));
    audit::expect_same_result(kernel::try_min(a), kernel::reference::try_min(a));
    audit::expect_same_result(kernel::try_max(a), kernel::reference::try_max(a));
    auto op = [](int acc, int x) { return acc + x; };
    EXPECT_EQ(kernel::reduce(a, 0, op), kernel::reference::reduce(a, 0, op));
  }
}

TEST(KernelAlgoAuditTest, OverlapErrorsMatchReference) {
  std::vector<int> buf{1, 2, 3, 4, 5, 6};
  auto out = span<int>(buf).subspan(1, 4);
  auto in = span<const int>(buf).subspan(0, 4);
  audit::expect_same_result(kernel::try_copy(out, in), kernel::reference::try_copy(out, in));
}

TEST(KernelAlgoAuditTest, FillAndInPlaceMatchReference) {
  std::vector<int> a{1, 2, 3}, b{1, 2, 3};
  kernel::fill(span<int>(a), 4);
  kernel::reference::fill(span<int>(b), 4);
  EXPECT_EQ(a, b);
  kernel::transform_in_place(span<int>(a), [](int x) { return x + 1; });
  kernel::reference::transform_in_place(span<int>(b), [](int x) { return x + 1; });
  EXPECT_EQ(a, b);
}

} // namespace

RELOCO_END_UNSAFE_BUFFER_USAGE
