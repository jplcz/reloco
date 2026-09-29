// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/tamper.hpp>

#include <cstdint>

namespace {

enum class vm_state : uint8_t {
  stopped,
  running,
  suspended,
};

} // namespace

TEST(TamperTest, MaskedIntegralSupportsArithmeticAndBitwiseUpdates) {
  reloco::masked_integral<uint32_t> value = 12;
  value += 4;
  value *= 2;
  EXPECT_EQ(static_cast<uint32_t>(value), 32u);

  value ^= 0x0Fu;
  EXPECT_EQ(static_cast<uint32_t>(value), 47u);
  EXPECT_EQ(value++, 47u);
  EXPECT_EQ(static_cast<uint32_t>(value), 48u);
  EXPECT_EQ(--value, 47u);
}

TEST(TamperTest, ProtectedStateSupportsConstructionAndAssignment) {
  reloco::tamper_proof_state<vm_state> state(vm_state::stopped);
  EXPECT_EQ(state.get(), vm_state::stopped);

  state = vm_state::running;
  EXPECT_EQ(state.get(), vm_state::running);
  EXPECT_EQ(static_cast<vm_state>(state), vm_state::running);
}

TEST(TamperTest, ProtectedBooleanSupportsBitwiseAndLogicalOperations) {
  reloco::tamper_bool enabled(true);
  reloco::tamper_bool disabled(false);

  EXPECT_TRUE(static_cast<bool>(enabled & enabled));
  EXPECT_FALSE(static_cast<bool>(enabled & disabled));
  EXPECT_TRUE(static_cast<bool>(enabled | disabled));
  EXPECT_TRUE(static_cast<bool>(enabled ^ disabled));
  EXPECT_FALSE(static_cast<bool>(!enabled));

  disabled = true;
  EXPECT_TRUE(static_cast<bool>(disabled));
}
