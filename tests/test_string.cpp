// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/string.hpp>
#include <reloco/unique_ptr.hpp>

#include <cstddef>
#include <string>
#include <utility>

using reloco::error;
using reloco::string;
using reloco::string_view;

TEST(StringTest, DefaultConstructedIsEmpty) {
  string s;
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.size(), 0u);
  EXPECT_EQ(s.capacity(), 0u);
  EXPECT_STREQ(s.data(), "");
}

TEST(StringTest, TryCreateFromView) {
  auto s = string::try_create(string_view("hello"));
  ASSERT_TRUE(s);
  EXPECT_EQ(s->size(), 5u);
  EXPECT_EQ(s->view(), "hello");
}

TEST(StringTest, TryAllocateUsesExplicitAllocator) {
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto s = string::try_allocate(heap, string_view("world"));
  ASSERT_TRUE(s);
  EXPECT_EQ(s->view(), "world");
}

TEST(StringTest, TryAppendGrowsAndConcatenates) {
  auto s = string::try_create();
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_append(string_view("foo")));
  ASSERT_TRUE(s->try_append(string_view("bar")));
  EXPECT_EQ(s->view(), "foobar");
  EXPECT_STREQ(s->data(), "foobar");
}

TEST(StringTest, TryAppendEmptyViewIsNoop) {
  auto s = string::try_create(string_view("abc"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_append(string_view()));
  EXPECT_EQ(s->view(), "abc");
}

TEST(StringTest, TryReserveGrowsCapacityWithoutChangingSize) {
  auto s = string::try_create(string_view("hi"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_reserve(64));
  EXPECT_GE(s->capacity(), 64u);
  EXPECT_EQ(s->size(), 2u);
  EXPECT_EQ(s->view(), "hi");
}

TEST(StringTest, TryAssignReplacesContent) {
  auto s = string::try_create(string_view("first"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_assign(string_view("second-longer")));
  EXPECT_EQ(s->view(), "second-longer");
}

TEST(StringTest, TryPushBackAndPopBack) {
  auto s = string::try_create();
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_push_back('a'));
  ASSERT_TRUE(s->try_push_back('b'));
  EXPECT_EQ(s->view(), "ab");
  s->pop_back();
  EXPECT_EQ(s->view(), "a");
}

TEST(StringTest, TryPopBackFailsOnEmpty) {
  auto s = string::try_create();
  ASSERT_TRUE(s);
  auto res = s->try_pop_back();
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::container_empty);
}

TEST(StringTest, TryInsertShiftsTail) {
  auto s = string::try_create(string_view("ac"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_insert(1, string_view("b")));
  EXPECT_EQ(s->view(), "abc");
}

TEST(StringTest, TryInsertOutOfBoundsFails) {
  auto s = string::try_create(string_view("ac"));
  ASSERT_TRUE(s);
  auto res = s->try_insert(10, string_view("x"));
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error(), error::out_of_bounds);
}

TEST(StringTest, EraseAndTryErase) {
  auto s = string::try_create(string_view("abcdef"));
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

TEST(StringTest, TryResizeGrowsAndShrinks) {
  auto s = string::try_create(string_view("ab"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_resize(5, 'x'));
  EXPECT_EQ(s->view(), "abxxx");
  ASSERT_TRUE(s->try_resize(1));
  EXPECT_EQ(s->view(), "a");
}

TEST(StringTest, ClearEmptiesButKeepsCapacity) {
  auto s = string::try_create(string_view("hello"));
  ASSERT_TRUE(s);
  const std::size_t cap_before = s->capacity();
  s->clear();
  EXPECT_TRUE(s->empty());
  EXPECT_EQ(s->capacity(), cap_before);
}

TEST(StringTest, ShrinkToFitReducesCapacity) {
  auto s = string::try_create(string_view("hi"));
  ASSERT_TRUE(s);
  ASSERT_TRUE(s->try_reserve(128));
  ASSERT_TRUE(s->shrink_to_fit());
  EXPECT_EQ(s->capacity(), s->size());
}

TEST(StringTest, ElementAccessTiers) {
  auto s = string::try_create(string_view("xyz"));
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

TEST(StringTest, IterationVisitsEachCharacter) {
  auto s = string::try_create(string_view("abc"));
  ASSERT_TRUE(s);
  std::string collected;
  for (char c : *s)
    collected.push_back(c);
  EXPECT_EQ(collected, "abc");
}

TEST(StringTest, SearchHelpers) {
  auto s = string::try_create(string_view("hello world"));
  ASSERT_TRUE(s);
  EXPECT_EQ(s->find(string_view("world")), 6u);
  EXPECT_TRUE(s->contains(string_view("lo w")));
  EXPECT_TRUE(s->starts_with(string_view("hello")));
  EXPECT_FALSE(s->contains(string_view("nope")));
}

TEST(StringTest, ComparisonOperators) {
  auto a = string::try_create(string_view("abc"));
  auto b = string::try_create(string_view("abc"));
  auto c = string::try_create(string_view("abd"));
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  ASSERT_TRUE(c);
  EXPECT_TRUE(*a == *b);
  EXPECT_TRUE(*a != *c);
  EXPECT_TRUE(*a < *c);
  EXPECT_TRUE(*a == string_view("abc"));
  EXPECT_TRUE(string_view("abc") == *a);
}

TEST(StringTest, ConversionsToStandardTypes) {
  auto s = string::try_create(string_view("conv"));
  ASSERT_TRUE(s);
  std::string_view std_view = *s;
  EXPECT_EQ(std_view, "conv");
  auto std_str = static_cast<std::string>(*s);
  EXPECT_EQ(std_str, "conv");
}

TEST(StringTest, MoveConstructionTransfersOwnership) {
  auto s = string::try_create(string_view("move-me"));
  ASSERT_TRUE(s);
  string moved(std::move(*s));
  EXPECT_EQ(moved.view(), "move-me");
  EXPECT_TRUE(s->empty());
}

TEST(StringTest, MoveAssignmentReleasesPreviousStorage) {
  auto a = string::try_create(string_view("first"));
  auto b = string::try_create(string_view("second"));
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  *a = std::move(*b);
  EXPECT_EQ(a->view(), "second");
  EXPECT_TRUE(b->empty());
}

TEST(StringTest, TryCloneDeepCopiesWithOwnAllocator) {
  auto s = string::try_create(string_view("clone-me"));
  ASSERT_TRUE(s);
  auto clone = s->try_clone();
  ASSERT_TRUE(clone);
  EXPECT_EQ(clone->view(), "clone-me");
  EXPECT_NE(clone->data(), s->data());
}

TEST(StringTest, TryCloneWithExplicitAllocator) {
  auto s = string::try_create(string_view("clone-me"));
  ASSERT_TRUE(s);
  reloco::allocator_ref heap = reloco::allocator<reloco::heap_allocator_tag>::ref();
  auto clone = s->try_clone(heap);
  ASSERT_TRUE(clone);
  EXPECT_EQ(clone->view(), "clone-me");
}

TEST(StringTest, ComposesWithUniquePtrViaTryCreateTier) {
  auto ptr = reloco::unique_ptr<string>::try_create(string_view("boxed"));
  ASSERT_TRUE(ptr);
  EXPECT_EQ((*ptr)->view(), "boxed");
}

TEST(StringTest, EmptyStringNeverAllocates) {
  auto s = string::try_create();
  ASSERT_TRUE(s);
  EXPECT_EQ(s->capacity(), 0u);
  EXPECT_NE(s->data(), nullptr);
}
