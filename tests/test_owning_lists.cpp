// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <iterator>
#include <reloco/circular_list.hpp>
#include <reloco/list.hpp>
#include <reloco/malloc_allocator.hpp>
#include <reloco/slist.hpp>
#include <reloco/stailq.hpp>
#include <reloco/tailq.hpp>
#include <string>
#include <vector>

namespace {

// Counts live instances so every test can verify that no node or element leaks.
struct tracked {
  static inline int live = 0;
  int value;

  explicit tracked(int v) noexcept : value(v) { ++live; }
  tracked(tracked &&other) noexcept : value(other.value) { ++live; }
  tracked(const tracked &other) noexcept : value(other.value) { ++live; }
  tracked &operator=(const tracked &) noexcept = default;
  ~tracked() { --live; }
};

template <typename C> std::vector<int> values(const C &c) {
  std::vector<int> out;
  for (const auto &v : c) {
    out.push_back(v.value);
  }
  return out;
}

template <typename C> class OwningListTest : public ::testing::Test {
protected:
  void TearDown() override { EXPECT_EQ(tracked::live, 0); }

  // Builds {1, 2, 3} using only front insertion, which every container supports.
  static C make_123() {
    C c;
    EXPECT_TRUE(c.try_emplace_front(3).has_value());
    EXPECT_TRUE(c.try_emplace_front(2).has_value());
    EXPECT_TRUE(c.try_emplace_front(1).has_value());
    return c;
  }
};

using all_lists = ::testing::Types<reloco::tailq<tracked>, reloco::stailq<tracked>, reloco::slist<tracked>,
                                   reloco::list<tracked>, reloco::circular_list<tracked>>;
TYPED_TEST_SUITE(OwningListTest, all_lists, ::testing::internal::DefaultNameGenerator);

TYPED_TEST(OwningListTest, StartsEmpty) {
  TypeParam c;
  EXPECT_TRUE(c.empty());
  EXPECT_EQ(c.size(), 0u);
  EXPECT_EQ(c.begin(), c.end());
  EXPECT_FALSE(c.try_front().has_value());
  EXPECT_FALSE(c.try_pop_front().has_value());
}

TYPED_TEST(OwningListTest, PushFrontKeepsOrderAndSize) {
  auto c = TestFixture::make_123();
  EXPECT_EQ(c.size(), 3u);
  EXPECT_EQ(values(c), (std::vector<int>{1, 2, 3}));
  EXPECT_EQ(c.front().value, 1);
}

TYPED_TEST(OwningListTest, PopFrontReturnsElements) {
  auto c = TestFixture::make_123();
  EXPECT_EQ(c.pop_front().value, 1);
  auto r = c.try_pop_front();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->value, 2);
  EXPECT_EQ(c.size(), 1u);
  EXPECT_EQ(c.front().value, 3);
}

TYPED_TEST(OwningListTest, InsertAfterAndRejectsEnd) {
  auto c = TestFixture::make_123();
  EXPECT_FALSE(c.try_emplace_after(c.cend(), 9).has_value());
  auto first = c.cbegin();
  ASSERT_TRUE(c.try_emplace_after(first, 9).has_value());
  EXPECT_EQ(values(c), (std::vector<int>{1, 9, 2, 3}));
  EXPECT_EQ(c.size(), 4u);
}

TYPED_TEST(OwningListTest, ReverseRelinksNodes) {
  auto c = TestFixture::make_123();
  const tracked *addr = &c.front();
  c.reverse();
  EXPECT_EQ(values(c), (std::vector<int>{3, 2, 1}));
  EXPECT_EQ(&*std::next(c.begin(), 2), addr); // same node, not a copy
  EXPECT_EQ(c.size(), 3u);
}

TYPED_TEST(OwningListTest, RetainPreservesOrder) {
  auto c = TestFixture::make_123();
  ASSERT_TRUE(c.try_emplace_front(4).has_value()); // {4, 1, 2, 3}
  c.retain([](const tracked &t) { return t.value != 1; });
  EXPECT_EQ(values(c), (std::vector<int>{4, 2, 3}));
  EXPECT_EQ(c.size(), 3u);
}

TYPED_TEST(OwningListTest, CloneIsDeepAndOrdered) {
  auto c = TestFixture::make_123();
  auto copy = c.try_clone();
  ASSERT_TRUE(copy.has_value());
  EXPECT_EQ(values(*copy), (std::vector<int>{1, 2, 3}));
  EXPECT_NE(&copy->front(), &c.front());
  c.clear();
  EXPECT_EQ(values(*copy), (std::vector<int>{1, 2, 3}));
}

TYPED_TEST(OwningListTest, AppendMovesNodes) {
  auto a = TestFixture::make_123();
  auto b = TestFixture::make_123();
  ASSERT_TRUE(a.try_append(std::move(b)).has_value());
  EXPECT_EQ(values(a), (std::vector<int>{1, 2, 3, 1, 2, 3}));
  EXPECT_EQ(a.size(), 6u);
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(b.size(), 0u);
}

TYPED_TEST(OwningListTest, AppendToEmpty) {
  TypeParam a;
  auto b = TestFixture::make_123();
  ASSERT_TRUE(a.try_append(std::move(b)).has_value());
  EXPECT_EQ(values(a), (std::vector<int>{1, 2, 3}));
}

TYPED_TEST(OwningListTest, PrependMovesNodesToFront) {
  auto a = TestFixture::make_123();
  auto b = TestFixture::make_123();
  ASSERT_TRUE(a.try_prepend(b).has_value());
  EXPECT_EQ(values(a), (std::vector<int>{1, 2, 3, 1, 2, 3}));
  EXPECT_EQ(a.size(), 6u);
  EXPECT_TRUE(b.empty());
  TypeParam empty;
  ASSERT_TRUE(empty.try_prepend(a).has_value());
  EXPECT_EQ(empty.size(), 6u);
  EXPECT_TRUE(empty.try_emplace_front(0).has_value()); // links remain valid
}

TYPED_TEST(OwningListTest, SpliceRejectsSelfAndForeignAllocator) {
  auto a = TestFixture::make_123();
  EXPECT_EQ(a.try_prepend(a).error(), reloco::error::invalid_argument);

  reloco::malloc_allocator<> heap(reloco::default_allocator(), 64 * 1024);
  auto other = TypeParam::try_allocate(heap.ref());
  ASSERT_TRUE(other.has_value());
  ASSERT_TRUE(other->try_emplace_front(7).has_value());
  ASSERT_TRUE(other->try_emplace_front(8).has_value());
  EXPECT_EQ(a.try_prepend(*other).error(), reloco::error::invalid_argument);
  EXPECT_EQ(a.try_append(std::move(*other)).error(), reloco::error::invalid_argument);
  EXPECT_EQ(a.size(), 3u);
  EXPECT_EQ(other->size(), 2u);
}

TYPED_TEST(OwningListTest, EmptyTargetAdoptsForeignAllocator) {
  reloco::malloc_allocator<> heap(reloco::default_allocator(), 64 * 1024);
  auto src = TypeParam::try_allocate(heap.ref());
  ASSERT_TRUE(src.has_value());
  ASSERT_TRUE(src->try_emplace_front(7).has_value());
  TypeParam dst; // default allocator, empty
  ASSERT_TRUE(dst.try_prepend(*src).has_value());
  EXPECT_EQ(dst.get_allocator(), heap.ref());
  EXPECT_EQ(values(dst), (std::vector<int>{7}));
  EXPECT_TRUE(src->empty());
  dst.clear(); // nodes are freed by the allocator that made them

  ASSERT_TRUE(src->try_emplace_front(8).has_value());
  TypeParam dst2;
  ASSERT_TRUE(dst2.try_append(std::move(*src)).has_value());
  EXPECT_EQ(dst2.get_allocator(), heap.ref());
}

TYPED_TEST(OwningListTest, MoveTransfersOwnership) {
  auto a = TestFixture::make_123();
  TypeParam b(std::move(a));
  EXPECT_TRUE(a.empty());
  EXPECT_EQ(values(b), (std::vector<int>{1, 2, 3}));
  EXPECT_TRUE(b.try_emplace_front(0).has_value()); // links still valid after the move
  TypeParam c;
  c = std::move(b);
  EXPECT_EQ(values(c), (std::vector<int>{0, 1, 2, 3}));
  EXPECT_EQ(c.size(), 4u);
}

template <typename C> class DoublySplice : public ::testing::Test {
protected:
  void TearDown() override { EXPECT_EQ(tracked::live, 0); }
};
using doubly_lists = ::testing::Types<reloco::tailq<tracked>, reloco::list<tracked>, reloco::circular_list<tracked>>;
TYPED_TEST_SUITE(DoublySplice, doubly_lists, ::testing::internal::DefaultNameGenerator);

template <typename C> C make_range(int from, int to) {
  C c;
  for (int i = to; i >= from; --i) {
    EXPECT_TRUE(c.try_emplace_front(i).has_value());
  }
  return c;
}

TYPED_TEST(DoublySplice, WholeListBeforePosition) {
  auto a = make_range<TypeParam>(1, 3);
  auto b = make_range<TypeParam>(10, 11);
  auto pos = a.cbegin();
  ++pos; // before 2
  ASSERT_TRUE(a.try_splice(pos, b).has_value());
  EXPECT_EQ(values(a), (std::vector<int>{1, 10, 11, 2, 3}));
  EXPECT_EQ(a.size(), 5u);
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(b.size(), 0u);
}

TYPED_TEST(DoublySplice, SingleElementAndRange) {
  auto a = make_range<TypeParam>(1, 3);
  auto b = make_range<TypeParam>(10, 14);
  auto it = b.cbegin();
  ++it; // 11
  ASSERT_TRUE(a.try_splice(a.cbegin(), b, it).has_value());
  EXPECT_EQ(values(a), (std::vector<int>{11, 1, 2, 3}));
  EXPECT_EQ(values(b), (std::vector<int>{10, 12, 13, 14}));

  auto first = b.cbegin();
  ++first;
  auto last = first;
  ++last;
  ++last; // [12, 13]
  ASSERT_TRUE(a.try_splice(a.cbegin(), b, first, last).has_value());
  EXPECT_EQ(values(a), (std::vector<int>{12, 13, 11, 1, 2, 3}));
  EXPECT_EQ(values(b), (std::vector<int>{10, 14}));
  EXPECT_EQ(a.size(), 6u);
  EXPECT_EQ(b.size(), 2u);
  b.erase(b.cbegin()); // source links stay valid
  EXPECT_EQ(values(b), (std::vector<int>{14}));
}

TYPED_TEST(DoublySplice, RejectsBadArguments) {
  auto a = make_range<TypeParam>(1, 2);
  auto b = make_range<TypeParam>(5, 6);
  EXPECT_EQ(a.try_splice(a.cbegin(), a).error(), reloco::error::invalid_argument);
  EXPECT_EQ(a.try_splice(a.cbegin(), b, b.cend()).error(), reloco::error::invalid_argument);
  EXPECT_EQ(a.size(), 2u);
  EXPECT_EQ(b.size(), 2u);
}

TEST(TailqTest, SpliceAtEndAppends) {
  auto a = make_range<reloco::tailq<tracked>>(1, 2);
  auto b = make_range<reloco::tailq<tracked>>(3, 4);
  ASSERT_TRUE(a.try_splice(a.cend(), b).has_value());
  EXPECT_EQ(values(a), (std::vector<int>{1, 2, 3, 4}));
  EXPECT_EQ(a.back().value, 4); // tail pointer updated
}

TEST(ListTest, SpliceAtEndIsRejected) {
  auto a = make_range<reloco::list<tracked>>(1, 2);
  auto b = make_range<reloco::list<tracked>>(3, 4);
  EXPECT_EQ(a.try_splice(a.cend(), b).error(), reloco::error::invalid_argument);
  EXPECT_EQ(b.size(), 2u);
}

template <typename C> class SinglySplice : public ::testing::Test {
protected:
  void TearDown() override { EXPECT_EQ(tracked::live, 0); }
};
using singly_lists = ::testing::Types<reloco::slist<tracked>, reloco::stailq<tracked>>;
TYPED_TEST_SUITE(SinglySplice, singly_lists, ::testing::internal::DefaultNameGenerator);

TYPED_TEST(SinglySplice, WholeListAfterPosition) {
  auto a = make_range<TypeParam>(1, 3);
  auto b = make_range<TypeParam>(10, 11);
  ASSERT_TRUE(a.try_splice_after(a.cbegin(), b).has_value());
  EXPECT_EQ(values(a), (std::vector<int>{1, 10, 11, 2, 3}));
  EXPECT_TRUE(b.empty());
  EXPECT_EQ(a.size(), 5u);
}

TYPED_TEST(SinglySplice, SingleAndRange) {
  auto a = make_range<TypeParam>(1, 2);
  auto b = make_range<TypeParam>(10, 14);
  ASSERT_TRUE(a.try_splice_after(a.cbegin(), b, b.cbegin()).has_value()); // moves 11
  EXPECT_EQ(values(a), (std::vector<int>{1, 11, 2}));
  EXPECT_EQ(values(b), (std::vector<int>{10, 12, 13, 14}));

  auto last = b.cbegin();
  ++last;
  ++last;
  ++last; // 14: moves (10, 14) = {12, 13}
  ASSERT_TRUE(a.try_splice_after(a.cbegin(), b, b.cbegin(), last).has_value());
  EXPECT_EQ(values(a), (std::vector<int>{1, 12, 13, 11, 2}));
  EXPECT_EQ(values(b), (std::vector<int>{10, 14}));
  EXPECT_EQ(a.size(), 5u);
  EXPECT_EQ(b.size(), 2u);
}

TYPED_TEST(SinglySplice, RangeToEndAndBadArguments) {
  auto a = make_range<TypeParam>(1, 2);
  auto b = make_range<TypeParam>(10, 12);
  ASSERT_TRUE(a.try_splice_after(a.cbegin(), b, b.cbegin(), b.cend()).has_value());
  EXPECT_EQ(values(a), (std::vector<int>{1, 11, 12, 2}));
  EXPECT_EQ(values(b), (std::vector<int>{10}));
  EXPECT_TRUE(b.try_emplace_front(9).has_value()); // tail/links stay consistent on the source
  EXPECT_EQ(a.try_splice_after(a.cend(), b).error(), reloco::error::invalid_argument);
  EXPECT_EQ(a.try_splice_after(a.cbegin(), b, std::next(b.cbegin())).error(), reloco::error::out_of_bounds);
  EXPECT_EQ(a.try_splice_after(a.cbegin(), a).error(), reloco::error::invalid_argument);
}

TEST(SinglyLinkedTest, EraseAfter) {
  reloco::slist<tracked> s;
  ASSERT_TRUE(s.try_emplace_front(3).has_value());
  ASSERT_TRUE(s.try_emplace_front(2).has_value());
  ASSERT_TRUE(s.try_emplace_front(1).has_value());
  auto next = s.erase_after(s.cbegin());
  EXPECT_EQ(next->value, 3);
  EXPECT_EQ(values(s), (std::vector<int>{1, 3}));
  EXPECT_FALSE(s.try_erase_after(std::next(s.cbegin())).has_value()); // nothing after the last element
  EXPECT_FALSE(s.try_erase_after(s.cend()).has_value());
}

TEST(SinglyLinkedTest, StailqEraseAfterLastUpdatesTail) {
  reloco::stailq<tracked> q;
  for (int i = 1; i <= 3; ++i) {
    ASSERT_TRUE(q.try_emplace_back(i).has_value());
  }
  auto next = q.erase_after(std::next(q.cbegin())); // removes 3, the tail
  EXPECT_EQ(next, q.end());
  EXPECT_EQ(q.back().value, 2);
  ASSERT_TRUE(q.try_emplace_back(4).has_value());
  EXPECT_EQ(values(q), (std::vector<int>{1, 2, 4}));
}

TEST(TailqTest, BothEnds) {
  reloco::tailq<tracked> q;
  ASSERT_TRUE(q.try_emplace_back(2).has_value());
  ASSERT_TRUE(q.try_emplace_back(3).has_value());
  ASSERT_TRUE(q.try_emplace_front(1).has_value());
  EXPECT_EQ(q.front().value, 1);
  EXPECT_EQ(q.back().value, 3);
  EXPECT_EQ(q.pop_back().value, 3);
  EXPECT_EQ(q.back().value, 2);
  EXPECT_EQ(q.pop_back().value, 2);
  EXPECT_EQ(q.pop_back().value, 1);
  EXPECT_FALSE(q.try_pop_back().has_value());
  EXPECT_FALSE(q.try_back().has_value());
}

TEST(TailqTest, EraseAndInsertBefore) {
  reloco::tailq<tracked> q;
  for (int i = 1; i <= 4; ++i) {
    ASSERT_TRUE(q.try_emplace_back(i).has_value());
  }
  auto it = q.erase(std::next(q.cbegin())); // erase 2
  EXPECT_EQ(it->value, 3);
  it = q.erase(std::next(q.cbegin(), 2)); // erase 4, the tail
  EXPECT_EQ(it, q.end());
  EXPECT_EQ(q.back().value, 3);
  ASSERT_TRUE(q.try_insert(q.cbegin(), tracked(0)).has_value());
  ASSERT_TRUE(q.try_insert(q.cend(), tracked(9)).has_value()); // before end() appends
  EXPECT_EQ(values(q), (std::vector<int>{0, 1, 3, 9}));
  EXPECT_EQ(q.back().value, 9);
}

TEST(ListTest, InsertBeforeEndIsRejected) {
  reloco::list<tracked> l;
  ASSERT_TRUE(l.try_emplace_front(1).has_value());
  EXPECT_FALSE(l.try_emplace(l.cend(), 2).has_value()); // no tail pointer, so no O(1) append
  ASSERT_TRUE(l.try_emplace(l.cbegin(), 0).has_value());
  EXPECT_EQ(values(l), (std::vector<int>{0, 1}));
  l.erase(l.cbegin());
  EXPECT_EQ(values(l), (std::vector<int>{1}));
}

TEST(CircularListTest, BidirectionalIteration) {
  reloco::circular_list<tracked> l;
  for (int i = 1; i <= 3; ++i) {
    ASSERT_TRUE(l.try_emplace_back(i).has_value());
  }
  auto it = l.end();
  std::vector<int> backwards;
  while (it != l.begin()) {
    --it;
    backwards.push_back(it->value);
  }
  EXPECT_EQ(backwards, (std::vector<int>{3, 2, 1}));
  EXPECT_EQ(l.back().value, 3);
  ASSERT_TRUE(l.try_insert(l.cend(), tracked(4)).has_value());
  ASSERT_TRUE(l.try_insert(std::next(l.cbegin()), tracked(7)).has_value());
  EXPECT_EQ(values(l), (std::vector<int>{1, 7, 2, 3, 4}));
  EXPECT_EQ(l.erase(std::next(l.cbegin())), std::next(l.begin()));
  EXPECT_EQ(l.pop_back().value, 4);
}

TEST(OwningListTest, WorksWithNonTrivialAndOverAlignedTypes) {
  struct alignas(64) wide {
    std::string s;
    explicit wide(std::string v) : s(std::move(v)) {}
  };
  reloco::tailq<wide> q;
  ASSERT_TRUE(q.try_emplace_back("a long string that defeats the small-string optimisation").has_value());
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(&q.front()) % 64, 0u);
  EXPECT_EQ(q.front().s.size(), 56u);
}

} // namespace
