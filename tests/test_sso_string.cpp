// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/sso_string.hpp>
#include <reloco/unique_ptr.hpp>

#include <cstddef>
#include <string>
#include <utility>

using reloco::error;
using reloco::sso_string;
using reloco::string_view;

namespace {
// Longer than sso_string::sso_capacity (15 by default) so it forces a heap
// allocation regardless of the configured capacity.
constexpr const char *kLongText = "this string is definitely longer than the inline sso buffer";
} // namespace

TEST(SsoStringTest, DefaultConstructedIsEmptyAndInline) {
  sso_string s;
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.size(), 0u);
  EXPECT_EQ(s.capacity(), sso_string::sso_capacity);
  EXPECT_TRUE(s.is_inline());
  EXPECT_STREQ(s.data(), "");
}

TEST(SsoStringTest, ShortStringStaysInline) {
  auto s = sso_string::try_create(string_view("short"));
  ASSERT_TRUE(s);
  EXPECT_TRUE(s->is_inline());
  EXPECT_EQ(s->view(), "short");
}

TEST(SsoStringTest, LongStringPromotesToHeap) {
  auto s = sso_string::try_create(string_view(kLongText));
  ASSERT_TRUE(s);
  EXPECT_FALSE(s->is_inline());
  EXPECT_EQ(s->view(), kLongText);
  EXPECT_GE(s->capacity(), s->size());
}

TEST(SsoStringTest, GrowingPastInlineCapacityPromotes) {
  auto s = sso_string::try_create(string_view("abc"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->is_inline());
  ASSERT_TRUE(s->try_append(string_view(kLongText)));
  EXPECT_FALSE(s->is_inline());
  EXPECT_EQ(s->size(), 3u + std::char_traits<char>::length(kLongText));
}

TEST(SsoStringTest, ShrinkToFitDemotesBackToInline) {
  auto s = sso_string::try_create(string_view(kLongText));
  ASSERT_TRUE(s);
  ASSERT_FALSE(s->is_inline());
  ASSERT_TRUE(s->try_resize(5));
  ASSERT_TRUE(s->shrink_to_fit());
  EXPECT_TRUE(s->is_inline());
  EXPECT_EQ(s->capacity(), sso_string::sso_capacity);
  EXPECT_EQ(s->view(), string_view(kLongText).substr(0, 5));
}

TEST(SsoStringTest, TryAllocateUsesExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto s = sso_string::try_allocate(heap, string_view("world"));
  ASSERT_TRUE(s);
  EXPECT_EQ(s->view(), "world");
}

TEST(SsoStringTest, TryAppendGrowsAndConcatenates) {
  auto s = sso_string::try_create();
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_append(string_view("foo")));
  ASSERT_TRUE(s->try_append(string_view("bar")));
  EXPECT_EQ(s->view(), "foobar");
  EXPECT_STREQ(s->data(), "foobar");
}

TEST(SsoStringTest, TryReserveGrowsCapacityWithoutChangingSize) {
  auto s = sso_string::try_create(string_view("hi"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_reserve(64));
  EXPECT_GE(s->capacity(), 64u);
  EXPECT_FALSE(s->is_inline());
  EXPECT_EQ(s->size(), 2u);
  EXPECT_EQ(s->view(), "hi");
}

TEST(SsoStringTest, TryAssignReplacesContent) {
  auto s = sso_string::try_create(string_view("first"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_assign(string_view("second-longer")));
  EXPECT_EQ(s->view(), "second-longer");
}

TEST(SsoStringTest, TryPushBackAndPopBack) {
  auto s = sso_string::try_create();
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_push_back('a'));
  ASSERT_TRUE(s->try_push_back('b'));
  EXPECT_EQ(s->view(), "ab");
  s->pop_back();
  EXPECT_EQ(s->view(), "a");
}

TEST(SsoStringTest, TryPopBackFailsOnEmpty) {
  auto s = sso_string::try_create();
  ASSERT_TRUE(s);
  auto res = s->try_pop_back();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::container_empty);
}

TEST(SsoStringTest, TryInsertShiftsTail) {
  auto s = sso_string::try_create(string_view("ac"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_insert(1, string_view("b")));
  EXPECT_EQ(s->view(), "abc");
}

TEST(SsoStringTest, TryInsertOutOfBoundsFails) {
  auto s = sso_string::try_create(string_view("ac"));
  ASSERT_TRUE(s);
  auto res = s->try_insert(10, string_view("x"));
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(SsoStringTest, EraseAndTryErase) {
  auto s = sso_string::try_create(string_view("abcdef"));
  ASSERT_TRUE(s);
  s->erase(1, 2);
  EXPECT_EQ(s->view(), "adef");

  auto ok = s->try_erase(0, 1);
  ASSERT_TRUE(ok);
  EXPECT_EQ(s->view(), "def");

  auto failed = s->try_erase(100);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), error::out_of_bounds);
}

TEST(SsoStringTest, TryResizeGrowsAndShrinks) {
  auto s = sso_string::try_create(string_view("ab"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_resize(5, 'x'));
  EXPECT_EQ(s->view(), "abxxx");
  ASSERT_TRUE(s->try_resize(1));
  EXPECT_EQ(s->view(), "a");
}

TEST(SsoStringTest, ClearEmptiesButKeepsCapacity) {
  auto s = sso_string::try_create(string_view("hello"));
  ASSERT_TRUE(s);
  const std::size_t cap_before = s->capacity();
  s->clear();
  EXPECT_TRUE(s->empty());
  EXPECT_EQ(s->capacity(), cap_before);
}

TEST(SsoStringTest, ElementAccessTiers) {
  auto s = sso_string::try_create(string_view("xyz"));
  ASSERT_TRUE(s);
  EXPECT_EQ(s->front(), 'x');
  EXPECT_EQ(s->back(), 'z');
  EXPECT_EQ((*s)[1], 'y');
  EXPECT_EQ(s->at(1), 'y');

  auto at_ok = s->try_at(0);
  ASSERT_TRUE(at_ok);
  EXPECT_EQ(at_ok->get(), 'x');

  auto at_bad = s->try_at(99);
  ASSERT_FALSE(at_bad);
  EXPECT_EQ(at_bad.error(), error::out_of_bounds);

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(s->unsafe_at(2), 'z');
  EXPECT_EQ(s->unsafe_front(), 'x');
  EXPECT_EQ(s->unsafe_back(), 'z');
  EXPECT_STREQ(s->unsafe_c_str(), "xyz");
  RELOCO_END_UNSAFE_BUFFER_USAGE
}

TEST(SsoStringTest, IterationVisitsEachCharacter) {
  auto s = sso_string::try_create(string_view("abc"));
  ASSERT_TRUE(s);
  std::string collected;
  for (char c : *s)
    collected.push_back(c);
  EXPECT_EQ(collected, "abc");
}

TEST(SsoStringTest, SearchHelpers) {
  auto s = sso_string::try_create(string_view("hello world"));
  ASSERT_TRUE(s);
  EXPECT_EQ(s->find(string_view("world")), 6u);
  EXPECT_TRUE(s->contains(string_view("lo w")));
  EXPECT_TRUE(s->starts_with(string_view("hello")));
  EXPECT_FALSE(s->contains(string_view("nope")));
}

TEST(SsoStringTest, ComparisonOperators) {
  auto a = sso_string::try_create(string_view("abc"));
  auto b = sso_string::try_create(string_view("abc"));
  auto c = sso_string::try_create(string_view("abd"));
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);
  EXPECT_TRUE(*a == *b);
  EXPECT_TRUE(*a != *c);
  EXPECT_TRUE(*a < *c);
  EXPECT_TRUE(*a == string_view("abc"));
  EXPECT_TRUE(string_view("abc") == *a);
}

TEST(SsoStringTest, ConversionsToStandardTypes) {
  auto s = sso_string::try_create(string_view("conv"));
  ASSERT_TRUE(s);
  std::string_view std_view = *s;
  EXPECT_EQ(std_view, "conv");
  auto std_str = static_cast<std::string>(*s);
  EXPECT_EQ(std_str, "conv");
}

TEST(SsoStringTest, MoveConstructionFromInlineCopiesBuffer) {
  auto s = sso_string::try_create(string_view("move-me"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->is_inline());
  sso_string moved(std::move(*s));
  EXPECT_TRUE(moved.is_inline());
  EXPECT_EQ(moved.view(), "move-me");
  EXPECT_TRUE(s->empty());
  EXPECT_TRUE(s->is_inline());
}

TEST(SsoStringTest, MoveConstructionFromHeapStealsPointer) {
  auto s = sso_string::try_create(string_view(kLongText));
  ASSERT_TRUE(s);
  ASSERT_FALSE(s->is_inline());
  const auto *original_data = s->data();
  sso_string moved(std::move(*s));
  EXPECT_FALSE(moved.is_inline());
  EXPECT_EQ(moved.data(), original_data);
  EXPECT_EQ(moved.view(), kLongText);
  EXPECT_TRUE(s->empty());
  EXPECT_TRUE(s->is_inline());
}

TEST(SsoStringTest, MoveAssignmentReleasesPreviousStorage) {
  auto a = sso_string::try_create(string_view(kLongText));
  auto b = sso_string::try_create(string_view("second"));
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  *a = std::move(*b);
  EXPECT_EQ(a->view(), "second");
  EXPECT_TRUE(b->empty());
}

TEST(SsoStringTest, TryCloneDeepCopiesWithOwnAllocator) {
  auto s = sso_string::try_create(string_view(kLongText));
  ASSERT_TRUE(s);
  auto clone = s->try_clone();
  ASSERT_TRUE(clone);
  EXPECT_EQ(clone->view(), kLongText);
  EXPECT_NE(clone->data(), s->data());
}

TEST(SsoStringTest, TryCloneWithExplicitAllocator) {
  auto s = sso_string::try_create(string_view("clone-me"));
  ASSERT_TRUE(s);
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto clone = s->try_clone(heap);
  ASSERT_TRUE(clone);
  EXPECT_EQ(clone->view(), "clone-me");
}

TEST(SsoStringTest, ComposesWithUniquePtrViaTryCreateTier) {
  auto ptr = reloco::unique_ptr<sso_string>::try_create(string_view("boxed"));
  ASSERT_TRUE(ptr);
  EXPECT_EQ((*ptr)->view(), "boxed");
}

TEST(SsoStringTest, EmptyStringNeverAllocates) {
  auto s = sso_string::try_create();
  ASSERT_TRUE(s);
  EXPECT_TRUE(s->is_inline());
  EXPECT_NE(s->data(), nullptr);
}

TEST(SsoStringTest, SelfAppendWithoutGrowthIsSafe) {
  auto s = sso_string::try_create(string_view("ab"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_reserve(16));
  ASSERT_TRUE(s->try_append(s->view()));
  EXPECT_EQ(s->view(), "abab");
}

TEST(SsoStringTest, SelfAppendForcingGrowthIsSafe) {
  auto s = sso_string::try_create(string_view("xy"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_append(s->view()));
  EXPECT_EQ(s->view(), "xyxy");
  ASSERT_TRUE(s->try_append(s->view()));
  ASSERT_TRUE(s->try_append(s->view()));
  EXPECT_FALSE(s->is_inline());
  EXPECT_EQ(s->size(), 16u);
}

TEST(SsoStringTest, SelfAssignWithOverlappingSubstringIsSafe) {
  auto s = sso_string::try_create(string_view("abcdef"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_assign(s->view().substr(2)));
  EXPECT_EQ(s->view(), "cdef");
}

TEST(SsoStringTest, SelfInsertOfWholeStringIsSafe) {
  auto s = sso_string::try_create(string_view("abcdef"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_insert(2, s->view()));
  EXPECT_EQ(s->view(), "ababcdefcdef");
}

TEST(SsoStringTest, SelfInsertOfOverlappingSubstringIsSafe) {
  auto s = sso_string::try_create(string_view("hello world"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_insert(0, s->view().substr(6)));
  EXPECT_EQ(s->view(), "worldhello world");
}

TEST(SsoStringTest, IsNotTriviallyRelocatable) {
  static_assert(!reloco::is_trivially_relocatable_v<sso_string>,
                "sso_string self-references its inline buffer while small, so it must never be treated as "
                "trivially relocatable");
}

TEST(SsoStringTest, WideCharVariantWorks) {
  auto s = reloco::wsso_string::try_create(reloco::basic_string_view<wchar_t>(L"hi"));
  ASSERT_TRUE(s);
  EXPECT_TRUE(s->is_inline());
  EXPECT_EQ(s->size(), 2u);
}
