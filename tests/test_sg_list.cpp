// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#if !defined(_MSC_VER) && defined(__LP64__)
#include <gtest/gtest.h>
#include <reloco/sg_list.hpp>

using dynamic_sg_list = reloco::sg_list<>;

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

TEST(SgListTest, PushBackDistinct) {
  dynamic_sg_list sgl;
  
  reloco::phys_addr<void, reloco::dma_bus_space> addr1(0x1000);
  reloco::phys_addr<void, reloco::dma_bus_space> addr2(0x8000); // Non-contiguous

  auto res1 = sgl.try_push_back(addr1, 4096);
  ASSERT_TRUE(res1.has_value());

  auto res2 = sgl.try_push_back(addr2, 8192);
  ASSERT_TRUE(res2.has_value());

  EXPECT_EQ(sgl.size(), 2u);
  
  auto it = sgl.begin();
  EXPECT_EQ(it->addr.value, 0x1000);
  EXPECT_EQ(it->length, 4096u);
  
  ++it;
  EXPECT_EQ(it->addr.value, 0x8000);
  EXPECT_EQ(it->length, 8192u);
}

RELOCO_END_UNSAFE_BUFFER_USAGE
#endif
