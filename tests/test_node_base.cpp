// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/detail/node_base.hpp>
#include <reloco/heap_allocator.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

using reloco::detail::metadata_for;
using reloco::detail::node_base;
using reloco::detail::type_metadata;

namespace {

reloco::allocator_ref heap() { return reloco::allocator<reloco::heap_allocator_tag>::ref(); }

} // namespace

TEST(NodeBaseTest, PayloadOffsetIsMultipleOfRequestedAlignment) {
  EXPECT_EQ(node_base::payload_offset(metadata_for<int>) % alignof(int), 0u);
  EXPECT_EQ(node_base::payload_offset(metadata_for<std::string>) % alignof(std::string), 0u);
}

TEST(NodeBaseTest, NodeAlignmentNeverWeakerThanHeaderAlignment) {
  EXPECT_GE(node_base::node_alignment(metadata_for<char>), node_base::header_alignment);
  EXPECT_EQ(node_base::node_alignment(metadata_for<int>), node_base::header_alignment);
}

TEST(NodeBaseTest, NodeSizeIsPayloadOffsetPlusElementSize) {
  const type_metadata &type = metadata_for<int>;
  EXPECT_EQ(node_base::node_size(type), node_base::payload_offset(type) + sizeof(int));
}

TEST(NodeBaseTest, PayloadAndHeaderRoundTrip) {
  const type_metadata &type = metadata_for<int>;
  auto node = node_base::try_allocate_node(heap(), type);
  ASSERT_TRUE(node);

  void *payload = node_base::payload_of(*node, type);
  EXPECT_EQ(node_base::header_of(payload, type), *node);

  const node_base::header *const_node = *node;
  const void *const_payload = node_base::payload_of(const_node, type);
  EXPECT_EQ(payload, const_payload);
  EXPECT_EQ(node_base::header_of(const_payload, type), const_node);

  node_base::deallocate_node(heap(), *node, type);
}

TEST(NodeBaseTest, FreshlyAllocatedHeaderHasNullLinks) {
  const type_metadata &type = metadata_for<int>;
  auto node = node_base::try_allocate_node(heap(), type);
  ASSERT_TRUE(node);
  EXPECT_EQ((*node)->parent, nullptr);
  EXPECT_EQ((*node)->left, nullptr);
  EXPECT_EQ((*node)->right, nullptr);
  EXPECT_EQ((*node)->aux, 0);
  node_base::deallocate_node(heap(), *node, type);
}

TEST(NodeBaseTest, PayloadIsUsableStorageForConstructingT) {
  const type_metadata &type = metadata_for<std::string>;
  auto node = node_base::try_allocate_node(heap(), type);
  ASSERT_TRUE(node);

  auto *str = ::new (node_base::payload_of(*node, type)) std::string("hello node_base");
  EXPECT_EQ(*str, "hello node_base");
  EXPECT_EQ(node_base::header_of(str, type), *node);

  str->~basic_string();
  node_base::deallocate_node(heap(), *node, type);
}

TEST(NodeBaseTest, OverAlignedPayloadStaysAligned) {
  struct alignas(64) over_aligned {
    int value;
  };
  const type_metadata &type = metadata_for<over_aligned>;
  auto node = node_base::try_allocate_node(heap(), type);
  ASSERT_TRUE(node);

  auto payload = reinterpret_cast<std::uintptr_t>(node_base::payload_of(*node, type));
  EXPECT_EQ(payload % 64, 0u);

  node_base::deallocate_node(heap(), *node, type);
}
