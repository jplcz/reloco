// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/bytes.hpp>

#include <cstring>
#include <utility>

using reloco::bytes;
using reloco::bytes_mut;

// This file compares raw byte contents with std::memcmp throughout, which
// clang flags as -Wunsafe-buffer-usage-in-libc-call; treated as a single
// checked boundary like reloco/bytes.hpp itself.
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

// Every allocation request fails, exercising the allocation-failure paths
// of bytes::try_copy_from, bytes_mut::try_reserve, and try_freeze.
struct exhausted_allocator_tag {};

reloco::span<const std::byte> str_span(const char *s) noexcept {
  return reloco::span<const std::byte>(reinterpret_cast<const std::byte *>(s), std::strlen(s));
}

} // namespace

template <> struct reloco::allocator_traits<exhausted_allocator_tag> {
  using context_type = void;

  static reloco::result<reloco::mem_block> allocate(std::size_t, std::size_t) noexcept {
    return reloco::unexpected(reloco::error::allocation_failed);
  }

  static void deallocate(void *, std::size_t) noexcept {}
};

// ---------------------------------------------------------------------------
// bytes_mut
// ---------------------------------------------------------------------------

TEST(BytesMutTest, DefaultConstructedIsEmpty) {
  reloco::allocator_ref alloc = reloco::default_allocator();
  bytes_mut buf(alloc);
  EXPECT_TRUE(buf.is_empty());
  EXPECT_EQ(buf.len(), 0u);
  EXPECT_EQ(buf.capacity(), 0u);
}

TEST(BytesMutTest, TryCreateWithInitialCapacity) {
  auto res = bytes_mut::try_create(128);
  ASSERT_TRUE(res.has_value());
  EXPECT_GE(res->capacity(), 128u);
  EXPECT_TRUE(res->is_empty());
}

TEST(BytesMutTest, TryPushGrowsAndAppends) {
  auto res = bytes_mut::try_create();
  ASSERT_TRUE(res.has_value());
  bytes_mut buf = std::move(*res);
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(buf.try_push(static_cast<std::byte>(i)));
  }
  EXPECT_EQ(buf.len(), 10u);
  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(buf[static_cast<std::size_t>(i)], static_cast<std::byte>(i));
  }
}

TEST(BytesMutTest, TryPutSliceAppendsSlice) {
  auto res = bytes_mut::try_create();
  ASSERT_TRUE(res.has_value());
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_slice(str_span("hello")));
  ASSERT_TRUE(buf.try_put_slice(str_span(" world")));
  EXPECT_EQ(buf.len(), 11u);
  EXPECT_EQ(std::memcmp(buf.data(), "hello world", 11), 0);
}

TEST(BytesMutTest, TryExtendFromSliceIsAliasForPutSlice) {
  auto res = bytes_mut::try_create();
  ASSERT_TRUE(res.has_value());
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_extend_from_slice(str_span("abc")));
  EXPECT_EQ(buf.len(), 3u);
}

TEST(BytesMutTest, PutU8) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_u8(0xAB));
  EXPECT_EQ(buf.len(), 1u);
  EXPECT_EQ(buf[0], std::byte{0xAB});
}

TEST(BytesMutTest, PutU16LittleEndian) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_u16_le(0x1234));
  ASSERT_EQ(buf.len(), 2u);
  EXPECT_EQ(buf[0], std::byte{0x34});
  EXPECT_EQ(buf[1], std::byte{0x12});
}

TEST(BytesMutTest, PutU16BigEndian) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_u16_be(0x1234));
  ASSERT_EQ(buf.len(), 2u);
  EXPECT_EQ(buf[0], std::byte{0x12});
  EXPECT_EQ(buf[1], std::byte{0x34});
}

TEST(BytesMutTest, PutU32LittleEndian) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_u32_le(0x01020304));
  ASSERT_EQ(buf.len(), 4u);
  EXPECT_EQ(buf[0], std::byte{0x04});
  EXPECT_EQ(buf[1], std::byte{0x03});
  EXPECT_EQ(buf[2], std::byte{0x02});
  EXPECT_EQ(buf[3], std::byte{0x01});
}

TEST(BytesMutTest, PutU32BigEndian) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_u32_be(0x01020304));
  ASSERT_EQ(buf.len(), 4u);
  EXPECT_EQ(buf[0], std::byte{0x01});
  EXPECT_EQ(buf[1], std::byte{0x02});
  EXPECT_EQ(buf[2], std::byte{0x03});
  EXPECT_EQ(buf[3], std::byte{0x04});
}

TEST(BytesMutTest, PutU64LittleEndian) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_u64_le(0x0102030405060708ULL));
  ASSERT_EQ(buf.len(), 8u);
  for (std::size_t i = 0; i < 8; ++i)
    EXPECT_EQ(buf[i], static_cast<std::byte>(8 - i));
}

TEST(BytesMutTest, PutU64BigEndian) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_u64_be(0x0102030405060708ULL));
  ASSERT_EQ(buf.len(), 8u);
  for (std::size_t i = 0; i < 8; ++i)
    EXPECT_EQ(buf[i], static_cast<std::byte>(i + 1));
}

TEST(BytesMutTest, ClearResetsLengthNotCapacity) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_slice(str_span("hello")));
  std::size_t cap_before = buf.capacity();
  buf.clear();
  EXPECT_EQ(buf.len(), 0u);
  EXPECT_TRUE(buf.is_empty());
  EXPECT_EQ(buf.capacity(), cap_before);
}

TEST(BytesMutTest, MoveConstructTransfersOwnership) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_slice(str_span("hello")));
  bytes_mut moved(std::move(buf));
  EXPECT_EQ(moved.len(), 5u);
  EXPECT_EQ(buf.len(), 0u); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  EXPECT_EQ(buf.data(), nullptr);
}

TEST(BytesMutTest, MoveAssignReleasesPreviousBuffer) {
  auto a_res = bytes_mut::try_create();
  bytes_mut a = std::move(*a_res);
  ASSERT_TRUE(a.try_put_slice(str_span("aaa")));

  auto b_res = bytes_mut::try_create();
  bytes_mut b = std::move(*b_res);
  ASSERT_TRUE(b.try_put_slice(str_span("bbbbb")));

  a = std::move(b);
  EXPECT_EQ(a.len(), 5u);
  EXPECT_EQ(std::memcmp(a.data(), "bbbbb", 5), 0);
}

TEST(BytesMutTest, ManyPushesGrowRepeatedly) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  for (int i = 0; i < 10000; ++i) {
    ASSERT_TRUE(buf.try_push(static_cast<std::byte>(i & 0xff)));
  }
  EXPECT_EQ(buf.len(), 10000u);
  for (int i = 0; i < 10000; ++i) {
    EXPECT_EQ(buf[static_cast<std::size_t>(i)], static_cast<std::byte>(i & 0xff));
  }
}

TEST(BytesMutTest, AsSpanReflectsCurrentContents) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_slice(str_span("xyz")));
  auto sp = buf.as_span();
  EXPECT_EQ(sp.size(), 3u);
  EXPECT_EQ(std::memcmp(sp.data(), "xyz", 3), 0);
}

TEST(BytesMutTest, TryReserveFailsWhenAllocatorExhausted) {
  reloco::allocator_ref alloc(exhausted_allocator_tag{});
  bytes_mut buf(alloc);
  auto res = buf.try_reserve(16);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), reloco::error::allocation_failed);
}

TEST(BytesMutTest, TryCreateFailsWhenAllocatorExhausted) {
  reloco::allocator_ref alloc(exhausted_allocator_tag{});
  auto res = bytes_mut::try_allocate(alloc, 16);
  ASSERT_FALSE(res.has_value());
}

// ---------------------------------------------------------------------------
// bytes_mut::try_freeze
// ---------------------------------------------------------------------------

TEST(BytesMutTest, FreezeEmptyBufferProducesEmptyBytes) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  auto frozen = std::move(buf).try_freeze();
  ASSERT_TRUE(frozen.has_value());
  EXPECT_TRUE(frozen->is_empty());
  EXPECT_EQ(frozen->size(), 0u);
}

TEST(BytesMutTest, FreezeNonEmptyBufferPreservesContents) {
  auto res = bytes_mut::try_create();
  bytes_mut buf = std::move(*res);
  ASSERT_TRUE(buf.try_put_slice(str_span("hello world")));
  auto frozen = std::move(buf).try_freeze();
  ASSERT_TRUE(frozen.has_value());
  EXPECT_EQ(frozen->size(), 11u);
  EXPECT_EQ(std::memcmp(frozen->data(), "hello world", 11), 0);
}

// ---------------------------------------------------------------------------
// bytes
// ---------------------------------------------------------------------------

TEST(BytesTest, DefaultConstructedIsEmpty) {
  bytes b;
  EXPECT_TRUE(b.is_empty());
  EXPECT_EQ(b.size(), 0u);
  EXPECT_EQ(b.data(), nullptr);
}

TEST(BytesTest, TryCopyFromEmptySpanProducesEmptyBytes) {
  auto res = bytes::try_copy_from(reloco::span<const std::byte>());
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(res->is_empty());
}

TEST(BytesTest, TryCopyFromCopiesData) {
  auto res = bytes::try_copy_from(str_span("hello"));
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->size(), 5u);
  EXPECT_EQ(std::memcmp(res->data(), "hello", 5), 0);
}

TEST(BytesTest, TryCopyFromFailsWhenAllocatorExhausted) {
  reloco::allocator_ref alloc(exhausted_allocator_tag{});
  auto res = bytes::try_copy_from(str_span("hello"), alloc);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), reloco::error::allocation_failed);
}

TEST(BytesTest, CopyIsCheapAndSharesStorage) {
  auto res = bytes::try_copy_from(str_span("hello"));
  ASSERT_TRUE(res.has_value());
  bytes a = std::move(*res);
  bytes b = a; // NOLINT(performance-unnecessary-copy-initialization)
  EXPECT_EQ(a.data(), b.data());
  EXPECT_EQ(a, b);
}

TEST(BytesTest, IndexOperatorReturnsByte) {
  auto res = bytes::try_copy_from(str_span("AB"));
  bytes b = std::move(*res);
  EXPECT_EQ(b[0], std::byte{'A'});
  EXPECT_EQ(b[1], std::byte{'B'});
}

TEST(BytesTest, TryAtChecksBounds) {
  auto res = bytes::try_copy_from(str_span("AB"));
  bytes b = std::move(*res);
  EXPECT_TRUE(b.try_at(1).has_value());
  auto oob = b.try_at(2);
  ASSERT_FALSE(oob.has_value());
  EXPECT_EQ(oob.error(), reloco::error::out_of_bounds);
}

TEST(BytesTest, SliceSharesStorageAndAdjustsView) {
  auto res = bytes::try_copy_from(str_span("hello world"));
  bytes b = std::move(*res);
  bytes mid = b.slice(6, 5);
  EXPECT_EQ(mid.size(), 5u);
  EXPECT_EQ(std::memcmp(mid.data(), "world", 5), 0);
}

TEST(BytesTest, TrySliceOutOfRangeFails) {
  auto res = bytes::try_copy_from(str_span("hello"));
  bytes b = std::move(*res);
  auto oob = b.try_slice(3, 10);
  ASSERT_FALSE(oob.has_value());
  EXPECT_EQ(oob.error(), reloco::error::out_of_bounds);
}

TEST(BytesTest, SplitToKeepsRemainderInOriginal) {
  auto res = bytes::try_copy_from(str_span("hello world"));
  bytes b = std::move(*res);
  bytes front = b.split_to(6);
  EXPECT_EQ(front.size(), 6u);
  EXPECT_EQ(std::memcmp(front.data(), "hello ", 6), 0);
  EXPECT_EQ(b.size(), 5u);
  EXPECT_EQ(std::memcmp(b.data(), "world", 5), 0);
}

TEST(BytesTest, SplitOffKeepsPrefixInOriginal) {
  auto res = bytes::try_copy_from(str_span("hello world"));
  bytes b = std::move(*res);
  bytes back = b.split_off(6);
  EXPECT_EQ(b.size(), 6u);
  EXPECT_EQ(std::memcmp(b.data(), "hello ", 6), 0);
  EXPECT_EQ(back.size(), 5u);
  EXPECT_EQ(std::memcmp(back.data(), "world", 5), 0);
}

TEST(BytesTest, TrySplitToOutOfRangeFails) {
  auto res = bytes::try_copy_from(str_span("abc"));
  bytes b = std::move(*res);
  auto oob = b.try_split_to(10);
  ASSERT_FALSE(oob.has_value());
  EXPECT_EQ(oob.error(), reloco::error::out_of_bounds);
}

TEST(BytesTest, TrySplitOffOutOfRangeFails) {
  auto res = bytes::try_copy_from(str_span("abc"));
  bytes b = std::move(*res);
  auto oob = b.try_split_off(10);
  ASSERT_FALSE(oob.has_value());
  EXPECT_EQ(oob.error(), reloco::error::out_of_bounds);
}

TEST(BytesTest, ClearReleasesStorage) {
  auto res = bytes::try_copy_from(str_span("abc"));
  bytes b = std::move(*res);
  b.clear();
  EXPECT_TRUE(b.is_empty());
  EXPECT_EQ(b.data(), nullptr);
}

TEST(BytesTest, EqualityComparesContentNotIdentity) {
  auto a = bytes::try_copy_from(str_span("same")).value();
  auto b = bytes::try_copy_from(str_span("same")).value();
  EXPECT_EQ(a, b); // different allocations, same content
  EXPECT_NE(a.data(), b.data());
}

TEST(BytesTest, InequalityForDifferentContent) {
  auto a = bytes::try_copy_from(str_span("aaa")).value();
  auto b = bytes::try_copy_from(str_span("bbb")).value();
  EXPECT_NE(a, b);
}

TEST(BytesTest, InequalityForDifferentLengths) {
  auto a = bytes::try_copy_from(str_span("aa")).value();
  auto b = bytes::try_copy_from(str_span("aaa")).value();
  EXPECT_NE(a, b);
}

TEST(BytesTest, AsSpanReflectsView) {
  auto res = bytes::try_copy_from(str_span("hello"));
  bytes b = std::move(*res);
  auto sp = b.as_span();
  EXPECT_EQ(sp.size(), 5u);
  EXPECT_EQ(std::memcmp(sp.data(), "hello", 5), 0);
}

TEST(BytesTest, SharingKeepsStorageAliveAfterOriginalCleared) {
  auto res = bytes::try_copy_from(str_span("hello"));
  bytes a = std::move(*res);
  bytes b = a;
  a.clear();
  EXPECT_TRUE(a.is_empty());
  ASSERT_EQ(b.size(), 5u);
  EXPECT_EQ(std::memcmp(b.data(), "hello", 5), 0);
}

TEST(BytesTest, IsTriviallyRelocatable) {
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<bytes>);
  EXPECT_TRUE(reloco::is_trivially_relocatable_v<bytes_mut>);
}

RELOCO_END_UNSAFE_BUFFER_USAGE
