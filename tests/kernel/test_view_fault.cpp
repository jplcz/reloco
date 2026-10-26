// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Exercises the fault points of try_make_tensor. RELOCO_ENABLE_FAULT_INJECTION is defined for this translation unit
// only, and the element type below is private to it, so no instantiation can clash with other test files (ODR).
#define RELOCO_ENABLE_FAULT_INJECTION

#include <gtest/gtest.h>
#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>

#include <vector>

namespace {

namespace kernel = reloco::kernel;

struct fault_elem {
  int value = 0;
};

TEST(KernelTensorFaultTest, RaisingTheMaxOffsetForcesOutOfBounds) {
  std::vector<fault_elem> data(6);
  auto ok = kernel::try_make_tensor<fault_elem, 2>(reloco::span<fault_elem>(data), {2, 3}, {3, 1});
  ASSERT_TRUE(ok.has_value());

  RELOCO_FAULT_INJECTOR(fi, kernel::fault::tensor_max_offset, [](std::size_t &max) { max = 1000; });
  auto failed = kernel::try_make_tensor<fault_elem, 2>(reloco::span<fault_elem>(data), {2, 3}, {3, 1});
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error(), reloco::error::out_of_bounds);
}

TEST(KernelTensorFaultTest, ClearingTheDisjointVerdictForcesInvalidArgument) {
  std::vector<fault_elem> data(6);
  RELOCO_FAULT_INJECTOR(fi, kernel::fault::tensor_disjoint, [](bool &disjoint) { disjoint = false; });
  auto failed = kernel::try_make_tensor<fault_elem, 2>(reloco::span<fault_elem>(data), {2, 3}, {3, 1});
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error(), reloco::error::invalid_argument);
}

} // namespace
