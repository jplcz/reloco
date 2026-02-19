#include "reloco/stack_allocator.hpp"

#include <gtest/gtest.h>
#include <reloco/shared_ptr.hpp>

struct TrackedNode : public reloco::enable_shared_from_this<TrackedNode> {
  static std::atomic<int> instances;
  int id;

  static reloco::result<TrackedNode> try_create(int id) {
    return TrackedNode(id);
  }

  explicit TrackedNode(int id) : id(id) { instances.fetch_add(1); }
  ~TrackedNode() { instances.fetch_sub(1); }
  TrackedNode(TrackedNode &&other) noexcept : id(other.id) {
    instances.fetch_add(1);
  }
  TrackedNode(const TrackedNode &) = delete;
  TrackedNode &operator=(const TrackedNode &) = delete;
};

std::atomic<int> TrackedNode::instances{0};

class SmartPointerTest : public ::testing::Test {
protected:
  // Using a large enough stack allocator for the combined blocks
  alignas(64) std::byte buffer[4096];
  reloco::stack_allocator alloc{buffer, sizeof(buffer)};

  void SetUp() override {
    TrackedNode::instances = 0;
    alloc.reset();
  }
};

TEST_F(SmartPointerTest, AllocationLifecycle) {
  {
    auto res = reloco::try_allocate_shared<TrackedNode>(alloc, 42);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(TrackedNode::instances, 1);
    EXPECT_EQ((*res)->id, 42);
    EXPECT_EQ(res->use_count(), 1);
  }
  // Object should be destroyed and memory reclaimed here
  EXPECT_EQ(TrackedNode::instances, 0);
}

TEST_F(SmartPointerTest, CombinedAllocationLifecycle) {
  {
    auto res = reloco::try_allocate_combined_shared<TrackedNode>(alloc, 42);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(TrackedNode::instances, 1);
    EXPECT_EQ((*res)->id, 42);
    EXPECT_EQ(res->use_count(), 1);
  }
  // Object should be destroyed and memory reclaimed here
  EXPECT_EQ(TrackedNode::instances, 0);
}

TEST_F(SmartPointerTest, CopyAndMoveSemantics) {
  auto res = reloco::try_allocate_shared<TrackedNode>(alloc, 1);
  auto ptr1 = std::move(*res);

  {
    reloco::shared_ptr<TrackedNode> ptr2 = ptr1; // Copy
    EXPECT_EQ(ptr1.use_count(), 2);
    EXPECT_EQ(ptr2.use_count(), 2);

    reloco::shared_ptr<TrackedNode> ptr3 = std::move(ptr2); // Move
    EXPECT_EQ(ptr1.use_count(), 2);
    EXPECT_EQ(ptr3.use_count(), 2);
    EXPECT_EQ(ptr2.get(), nullptr);
  }
  EXPECT_EQ(ptr1.use_count(), 1);
}

TEST_F(SmartPointerTest, CombinedCopyAndMoveSemantics) {
  auto res = reloco::try_allocate_combined_shared<TrackedNode>(alloc, 1);
  auto ptr1 = std::move(*res);

  {
    reloco::shared_ptr<TrackedNode> ptr2 = ptr1; // Copy
    EXPECT_EQ(ptr1.use_count(), 2);
    EXPECT_EQ(ptr2.use_count(), 2);

    reloco::shared_ptr<TrackedNode> ptr3 = std::move(ptr2); // Move
    EXPECT_EQ(ptr1.use_count(), 2);
    EXPECT_EQ(ptr3.use_count(), 2);
    EXPECT_EQ(ptr2.get(), nullptr);
  }
  EXPECT_EQ(ptr1.use_count(), 1);
}

TEST_F(SmartPointerTest, WeakPtrLocking) {
  reloco::weak_ptr<TrackedNode> w;
  {
    auto s = reloco::try_allocate_shared<TrackedNode>(alloc, 100).value();
    w = s;
    EXPECT_FALSE(w.expired());

    auto locked = w.lock();
    ASSERT_TRUE(locked.has_value());
    EXPECT_EQ((*locked)->id, 100);
    EXPECT_EQ(locked->use_count(), 2);
  }
  // Shared count is 0, but weak count is 1.
  // Control block should still exist, but object is destroyed.
  EXPECT_TRUE(w.expired());
  EXPECT_EQ(TrackedNode::instances, 0);

  auto locked_fail = w.lock();
  EXPECT_FALSE(locked_fail.has_value()); // Should return error::pointer_expired
}

TEST_F(SmartPointerTest, CombinedWeakPtrLocking) {
  reloco::weak_ptr<TrackedNode> w;
  {
    auto s =
        reloco::try_allocate_combined_shared<TrackedNode>(alloc, 100).value();
    w = s;
    EXPECT_FALSE(w.expired());

    auto locked = w.lock();
    ASSERT_TRUE(locked.has_value());
    EXPECT_EQ((*locked)->id, 100);
    EXPECT_EQ(locked->use_count(), 2);
  }
  // Shared count is 0, but weak count is 1.
  // Control block should still exist, but object is destroyed.
  EXPECT_TRUE(w.expired());
  EXPECT_EQ(TrackedNode::instances, 0);

  auto locked_fail = w.lock();
  EXPECT_FALSE(locked_fail.has_value()); // Should return error::pointer_expired
}

TEST_F(SmartPointerTest, EnableSharedFromThisSuccess) {
  auto s = reloco::try_allocate_shared<TrackedNode>(alloc, 7).value();

  auto sft_res = s->shared_from_this();
  ASSERT_TRUE(sft_res.has_value())
      << "No value? " << static_cast<int>(sft_res.error());
  EXPECT_EQ(sft_res->use_count(), 2);
  EXPECT_EQ((*sft_res)->id, 7);
}

TEST_F(SmartPointerTest, EnableSharedFromCombinedThisSuccess) {
  auto s = reloco::try_allocate_combined_shared<TrackedNode>(alloc, 7).value();

  auto sft_res = s->shared_from_this();
  ASSERT_TRUE(sft_res.has_value())
      << "No value? " << static_cast<int>(sft_res.error());
  EXPECT_EQ(sft_res->use_count(), 2);
  EXPECT_EQ((*sft_res)->id, 7);
}

TEST_F(SmartPointerTest, EnableSharedFromThisFailure) {
  // Creating a node on the stack (not managed by shared_ptr)
  TrackedNode stack_node(99);

  auto sft_res = stack_node.shared_from_this();
  // In reloco, this returns a result error instead of throwing
  EXPECT_FALSE(sft_res.has_value());
}

TEST_F(SmartPointerTest, SeparateAllocationFailsCleanly) {
  struct MockFailAlloc : public reloco::stack_allocator {
    int call_count = 0;
    using stack_allocator::stack_allocator;

    reloco::result<reloco::mem_block> allocate(size_t b,
                                               size_t a) noexcept override {
      if (++call_count == 2)
        return std::unexpected(reloco::error::allocation_failed);
      return stack_allocator::allocate(b, a);
    }
  } fail_alloc{buffer, sizeof(buffer)};

  // Attempt separate allocation
  auto res = reloco::try_allocate_shared<TrackedNode>(fail_alloc, 1);
  ASSERT_FALSE(res.has_value());
  // Instances should be 0 because the object was destroyed
  // when the control block allocation failed.
  EXPECT_EQ(TrackedNode::instances, 0);
}

TEST_F(SmartPointerTest, DefaultAllocatorCombinedIntegration) {
  // This test verifies the bridge to the system allocator
  auto res = reloco::try_make_combined_shared<TrackedNode>(99);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ((*res)->id, 99);
  EXPECT_EQ(TrackedNode::instances, 1);

  res->reset();
  EXPECT_EQ(TrackedNode::instances, 0);
}

TEST_F(SmartPointerTest, DefaultAllocatorIntegration) {
  // This test verifies the bridge to the system allocator
  auto res = reloco::try_make_shared<TrackedNode>(99);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ((*res)->id, 99);
  EXPECT_EQ(TrackedNode::instances, 1);

  res->reset();
  EXPECT_EQ(TrackedNode::instances, 0);
}

TEST_F(SmartPointerTest, CombinedRespectsStrictAlignment) {
  struct alignas(64) AlignedType {
    float data[16];
    static reloco::result<AlignedType> try_create() { return AlignedType{}; }
  };

  auto res = reloco::try_allocate_combined_shared<AlignedType>(alloc);

  ASSERT_TRUE(res.has_value());
  uintptr_t addr = reinterpret_cast<uintptr_t>(res->get());
  EXPECT_EQ(addr % 64, 0) << "Object in combined block not aligned to 64 bytes";
}

TEST_F(SmartPointerTest, PlainRespectsStrictAlignment) {
  struct alignas(64) AlignedType {
    float data[16];
    static reloco::result<AlignedType> try_create() { return AlignedType{}; }
  };

  auto res = reloco::try_allocate_shared<AlignedType>(alloc);

  ASSERT_TRUE(res.has_value());
  uintptr_t addr = reinterpret_cast<uintptr_t>(res->get());
  EXPECT_EQ(addr % 64, 0) << "Object in combined block not aligned to 64 bytes";
}

struct RecursiveNode : public reloco::enable_shared_from_this<RecursiveNode> {
  reloco::shared_ptr<RecursiveNode> inner;

  static reloco::result<RecursiveNode> try_create(reloco::fallible_allocator &a,
                                                  bool recurse) {
    RecursiveNode node;
    if (recurse) {
      auto res =
          reloco::try_allocate_combined_shared<RecursiveNode>(a, a, false);
      if (res)
        node.inner = std::move(*res);
    }
    return node;
  }
};

TEST_F(SmartPointerTest, HandlesRecursiveAllocation) {
  auto res =
      reloco::try_allocate_combined_shared<RecursiveNode>(alloc, alloc, true);
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE((*res)->inner);
}
