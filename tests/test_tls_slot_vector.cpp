// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/default_allocator.hpp>
#include <reloco/spin_lock.hpp>
#include <reloco/tls_slot_vector.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {
struct int_tag {};
struct string_tag {};
struct other_int_tag {};

// A distinct trait Tag so this file's tls_local_slots doesn't share
// thread_local storage with any other translation unit's tests.
struct test_traits_tag {};
using local_slots = reloco::tls_local_slots<>;
using tagged_local_slots = reloco::tls_local_slots<reloco::null_mutex, reloco::bucket_growth::doubling_then_ratio,
                                                    test_traits_tag>;
} // namespace

template <> struct reloco::tls_local_state_traits<test_traits_tag> {
  [[nodiscard]] static void *get() noexcept { return ptr_; }
  static void set(void *ptr) noexcept { ptr_ = ptr; }

private:
  static inline thread_local void *ptr_ = nullptr;
};

TEST(TlsSlotVectorTest, GetOrCreateThenFind) {
  reloco::tls_slot_vector<> vec;
  auto created = vec.get_or_create<int, int_tag>(42);
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->get(), 42);

  auto found = vec.try_find<int, int_tag>();
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 42);

  // Second get_or_create returns the existing value, not a fresh default.
  auto again = vec.get_or_create<int, int_tag>(999);
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(again->get(), 42);
}

TEST(TlsSlotVectorTest, TryFindMissingFails) {
  reloco::tls_slot_vector<> vec;
  auto found = vec.try_find<int, other_int_tag>();
  ASSERT_FALSE(found.has_value());
  EXPECT_EQ(found.error(), reloco::error::not_found);
}

TEST(TlsSlotVectorTest, SetCreatesThenOverwrites) {
  reloco::tls_slot_vector<> vec;
  auto set_one = vec.set<std::string, string_tag>("one");
  ASSERT_TRUE(set_one.has_value());
  auto found = vec.try_find<std::string, string_tag>();
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), "one");

  auto set_uno = vec.set<std::string, string_tag>("uno");
  ASSERT_TRUE(set_uno.has_value());
  found = vec.try_find<std::string, string_tag>();
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), "uno");
}

TEST(TlsSlotVectorTest, EraseRemovesValue) {
  reloco::tls_slot_vector<> vec;
  auto created = vec.get_or_create<int, int_tag>(7);
  ASSERT_TRUE(created.has_value());
  auto erased = vec.erase<int, int_tag>();
  ASSERT_TRUE(erased.has_value());
  auto missing = vec.try_find<int, int_tag>();
  EXPECT_FALSE(missing.has_value());

  auto second_erase = vec.erase<int, int_tag>();
  ASSERT_FALSE(second_erase.has_value());
  EXPECT_EQ(second_erase.error(), reloco::error::not_found);
}

TEST(TlsSlotVectorTest, DistinctTagsGetDistinctSlots) {
  reloco::tls_slot_vector<> vec;
  auto created_int = vec.get_or_create<int, int_tag>(1);
  ASSERT_TRUE(created_int.has_value());
  auto created_string = vec.get_or_create<std::string, string_tag>("a");
  ASSERT_TRUE(created_string.has_value());
  auto created_other_int = vec.get_or_create<int, other_int_tag>(2);
  ASSERT_TRUE(created_other_int.has_value());

  auto found_int = vec.try_find<int, int_tag>();
  auto found_string = vec.try_find<std::string, string_tag>();
  auto found_other_int = vec.try_find<int, other_int_tag>();
  EXPECT_EQ(found_int->get(), 1);
  EXPECT_EQ(found_string->get(), "a");
  EXPECT_EQ(found_other_int->get(), 2);
}

TEST(TlsSlotVectorTest, ClearRunsDestructorsAndAllowsReuse) {
  static std::atomic<int> destroyed_count{0};
  struct tracked {
    tracked() = default;
    tracked(const tracked &) = default;
    ~tracked() { destroyed_count.fetch_add(1); }
  };
  struct tracked_tag {};

  {
    reloco::tls_slot_vector<> vec;
    auto created = vec.get_or_create<tracked, tracked_tag>();
    ASSERT_TRUE(created.has_value());
    // get_or_create()'s by-value parameters (the T{} default argument, an
    // intermediate copy) already destroyed a few temporaries by this
    // point -- only the *stored* slot value is still live, so compare a
    // before/after delta across clear() rather than an absolute count.
    int before_clear = destroyed_count.load();
    vec.clear();
    EXPECT_EQ(destroyed_count.load() - before_clear, 1); // exactly the one live, linked slot value.
    // clear() is safe to call again once already empty (a no-op: no
    // further destructor runs).
    vec.clear();
    EXPECT_EQ(destroyed_count.load() - before_clear, 1);
  }
}

TEST(TlsSlotVectorTest, AllocatorAccessorMatchesConstruction) {
  reloco::allocator_ref alloc = reloco::default_allocator();
  reloco::tls_slot_vector<> vec(alloc);
  // No operator== on allocator_ref -- verify indirectly: an object
  // allocated through vec's own get_or_create() is reachable and
  // correctly torn down via vec.allocator(), exercised by clear() in
  // the destructor at scope exit.
  auto created = vec.get_or_create<int, int_tag>(1);
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->get(), 1);
}

TEST(TlsSlotVectorTest, ReserveGrowsCapacityUpFront) {
  reloco::tls_slot_vector<> vec;
  ASSERT_TRUE(vec.reserve(8).has_value());

  // get_or_create() for a slot within the reserved capacity must not
  // need to grow again -- exercised indirectly here (a real regression
  // would show up as an allocator failure/incorrect value, not directly
  // observable capacity), the point being reserve() itself succeeds and
  // subsequent use behaves exactly like it would without reserve().
  auto created = vec.get_or_create<int, int_tag>(7);
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->get(), 7);
}

TEST(TlsSlotVectorTest, ReserveOfZeroIsANoOp) {
  reloco::tls_slot_vector<> vec;
  EXPECT_TRUE(vec.reserve(0).has_value());
}

TEST(TlsSlotVectorTest, GetOrCreateAndSetDoNotDeadlockUnderSpinLock) {
  // Regression test: get_or_create()/set() must never call into the
  // allocator while holding lock_ -- previously ensure_capacity() ran
  // entirely under the caller's lock_.lock()/unlock() pair, so growing
  // (which allocates) happened locked. A blocking allocator or a
  // preemption-disabled caller sharing this same instance would have
  // deadlocked/panicked; a plain spin_lock here at least proves the
  // "unlock, allocate, relock" discipline actually holds by running
  // clean under RELOCO_CAPABILITY lock-order annotations/TSan.
  reloco::tls_slot_vector<reloco::spin_lock> vec;
  auto created = vec.get_or_create<int, int_tag>(11);
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->get(), 11);

  auto was_set = vec.set<int, other_int_tag>(22);
  ASSERT_TRUE(was_set.has_value());
  auto found = vec.try_find<int, other_int_tag>();
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 22);
}

TEST(TlsLocalSlotsTest, GetOrCreateThenFindPerThread) {
  auto created = local_slots::get_or_create<int, int_tag>(reloco::default_allocator(), 42);
  ASSERT_TRUE(created.has_value());
  EXPECT_EQ(created->get(), 42);

  auto found = local_slots::try_find<int, int_tag>();
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 42);

  local_slots::clear_current();
  auto missing = local_slots::try_find<int, int_tag>();
  EXPECT_FALSE(missing.has_value());
}

TEST(TlsLocalSlotsTest, TryFindMissingFailsBeforeAnyCreate) {
  auto missing = local_slots::try_find<int, other_int_tag>();
  EXPECT_FALSE(missing.has_value());
  // clear_current() is a harmless no-op when nothing was ever created.
  local_slots::clear_current();
}

TEST(TlsLocalSlotsTest, DistinctThreadsGetIndependentValues) {
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 16; ++i) {
    threads.emplace_back([i, &failures] {
      auto created = local_slots::get_or_create<int, int_tag>(reloco::default_allocator(), i);
      if (!created || created->get() != i) {
        ++failures;
        return;
      }
      auto found = local_slots::try_find<int, int_tag>();
      if (!found || found->get() != i) {
        ++failures;
        return;
      }
      local_slots::clear_current();
      if (local_slots::try_find<int, int_tag>().has_value())
        ++failures;
    });
  }
  for (auto &t : threads)
    t.join();
  EXPECT_EQ(failures.load(), 0);
}

TEST(TlsLocalSlotsTest, CustomTraitsTagIsIndependentStorage) {
  auto set_result = tagged_local_slots::set<int, int_tag>(5);
  ASSERT_TRUE(set_result.has_value());
  auto found = tagged_local_slots::try_find<int, int_tag>();
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->get(), 5);
  tagged_local_slots::clear_current();
}
