// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/hint.hpp>

TEST(HintTest, SpinLoopIsSafeToCallRepeatedly) {
  for (int i = 0; i < 1000; ++i)
    reloco::hint::spin_loop();
  SUCCEED();
}
