// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Exercises the fault points of try_bind. RELOCO_ENABLE_FAULT_INJECTION is defined for this translation unit only.
#define RELOCO_ENABLE_FAULT_INJECTION

#include <gtest/gtest.h>
#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>

#include <vector>

namespace {

namespace kernel = reloco::kernel;
struct fault_brand {};

TEST(KernelBindFaultTest, ClearingTheExtentVerdictForcesOutOfBounds) {
  std::vector<int> data(4);
  RELOCO_FAULT_INJECTOR(fi, kernel::fault::bind_extent, [](bool &ok) { ok = false; });
  auto failed = kernel::try_bind<fault_brand>(4, reloco::span<int>(data));
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error(), reloco::error::out_of_bounds);
}

TEST(KernelBindFaultTest, ClearingTheDisjointVerdictForcesInvalidArgument) {
  std::vector<int> data(4);
  RELOCO_FAULT_INJECTOR(fi, kernel::fault::bind_disjoint, [](bool &ok) { ok = false; });
  auto failed = kernel::try_bind<fault_brand>(4, reloco::span<int>(data));
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error(), reloco::error::invalid_argument);
}

} // namespace
