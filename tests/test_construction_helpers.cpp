#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/construction_helpers.hpp>
#include <reloco/stack_allocator.hpp>

namespace {

struct TypeWithConstruct {
  bool constructed = false;
  TypeWithConstruct() noexcept = default;
  reloco::result<void> try_construct(int val) noexcept {
    constructed = (val == 42);
    return constructed ? reloco::result<void>{}
                       : reloco::unexpected(reloco::error::invalid_argument);
  }
};

struct TypeWithAllocate {
  int value;
  static reloco::result<TypeWithAllocate>
  try_allocate(reloco::fallible_allocator &, int val) noexcept {
    return TypeWithAllocate{val};
  }
};

struct TypeWithClone {
  int id;
  bool cloned = false;
  reloco::result<TypeWithClone>
  try_clone(reloco::fallible_allocator &) const noexcept {
    return TypeWithClone{id, true};
  }
};

struct FailConstruct {
  inline static thread_local bool destroyed = false;
  FailConstruct() noexcept { destroyed = false; }
  ~FailConstruct() { destroyed = true; }
  reloco::result<void> try_construct() noexcept {
    return reloco::unexpected(reloco::error::allocation_failed);
  }
};

struct MoveOnlyFallible {
  int id;
  MoveOnlyFallible(int i) : id(i) {}
  MoveOnlyFallible(const MoveOnlyFallible &) = delete;
  MoveOnlyFallible(MoveOnlyFallible &&) noexcept = default;

  static reloco::result<MoveOnlyFallible>
  try_allocate(reloco::fallible_allocator &, int i) {
    return MoveOnlyFallible{i};
  }
};

struct ComplexShell {
  std::atomic<void *> resource;
  ComplexShell() noexcept : resource(nullptr) {
    // Allocating a small internal tracking buffer that succeeds
    resource = (void *)0xDEADBEEF;
  }
  ~ComplexShell() { resource = nullptr; }
  reloco::result<void> try_construct(bool fail) noexcept {
    if (fail)
      return reloco::unexpected(reloco::error::allocation_failed);
    return {};
  }
};

} // namespace

class ConstructionHelpersTest : public ::testing::Test {
protected:
  static constexpr std::size_t kBufferSize = 8192;
  alignas(64) std::byte buffer[kBufferSize]{};
  reloco::stack_allocator alloc{buffer, kBufferSize};

  void SetUp() override { alloc.reset(); }
};

TEST_F(ConstructionHelpersTest, ConstructPrefersTwoPhase) {
  alignas(TypeWithConstruct) std::byte buffer[sizeof(TypeWithConstruct)];
  auto *ptr = reinterpret_cast<TypeWithConstruct *>(buffer);

  auto res = reloco::construction_helpers::try_construct(alloc, ptr, 42);

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(ptr->constructed);
  ptr->~TypeWithConstruct();
}

TEST_F(ConstructionHelpersTest, ConstructRollsBackOnFailure) {
  alignas(FailConstruct) std::byte buffer[sizeof(FailConstruct)];
  auto *ptr = reinterpret_cast<FailConstruct *>(buffer);

  auto res = reloco::construction_helpers::try_construct(alloc, ptr);

  EXPECT_FALSE(res.has_value());
  // Verify the helper called the destructor after try_construct failed
  EXPECT_TRUE(FailConstruct::destroyed);
}

TEST_F(ConstructionHelpersTest, AllocatePrefersFactory) {
  // Should call TypeWithAllocate::try_allocate
  auto res =
      reloco::construction_helpers::try_allocate<TypeWithAllocate>(alloc, 100);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->value, 100);
}

TEST_F(ConstructionHelpersTest, AllocateFallsBackToNothrowCtor) {
  struct Simple {
    int x;
    Simple(int val) noexcept : x(val) {}
  };

  auto res = reloco::construction_helpers::try_allocate<Simple>(alloc, 5);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->x, 5);
}

TEST_F(ConstructionHelpersTest, ClonePrefersCustomMethod) {
  TypeWithClone original{77, false};

  auto res = reloco::construction_helpers::try_clone(alloc, original);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->id, 77);
  EXPECT_TRUE(res->cloned); // Proves custom try_clone was hit
}

TEST_F(ConstructionHelpersTest, CloneFallsBackToCopyCtor) {
  struct POD {
    int a;
  };
  POD original{10};

  auto res = reloco::construction_helpers::try_clone(alloc, original);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->a, 10);
}

TEST_F(ConstructionHelpersTest, ConceptValidation) {
  // TypeWithConstruct
  static_assert(reloco::has_try_construct<TypeWithConstruct, int>);
  static_assert(!reloco::has_try_allocate<TypeWithConstruct, int>);

  // TypeWithAllocate
  static_assert(reloco::has_try_allocate<TypeWithAllocate, int>);

  // TypeWithClone
  static_assert(reloco::has_try_clone<TypeWithClone>);
}

TEST_F(ConstructionHelpersTest, AllocateHandlesMoveOnlyTypes) {
  // This test ensures that try_allocate correctly moves the result of
  // T::try_allocate into the return value without attempting a copy.
  auto res =
      reloco::construction_helpers::try_allocate<MoveOnlyFallible>(alloc, 42);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->id, 42);
}

TEST_F(ConstructionHelpersTest, TryConstructCleansUpShellOnInternalFailure) {
  alignas(ComplexShell) std::byte buffer[sizeof(ComplexShell)];
  auto *ptr = reinterpret_cast<ComplexShell *>(buffer);

  // try_construct will return an error
  auto res = reloco::construction_helpers::try_construct(alloc, ptr, true);

  ASSERT_FALSE(res.has_value());
  // The helper MUST have called the destructor.
  // We check if the pointer's internal state was cleared by the destructor.
  EXPECT_EQ(ptr->resource, nullptr);
}

TEST_F(ConstructionHelpersTest, HandlesEmptyArgumentForwarding) {
  struct NoArgConstruct {
    bool hit = false;
    reloco::result<void> try_construct() noexcept {
      hit = true;
      return {};
    }
  };

  alignas(NoArgConstruct) std::byte buffer[sizeof(NoArgConstruct)];
  auto *ptr = reinterpret_cast<NoArgConstruct *>(buffer);

  auto res = reloco::construction_helpers::try_construct(alloc, ptr);

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(ptr->hit);
  ptr->~NoArgConstruct();
}

TEST_F(ConstructionHelpersTest, CloneFallsBackToAllocateWithCorrectAllocator) {
  static thread_local reloco::fallible_allocator *captured_alloc = nullptr;

  struct AllocatorSniffer {
    static reloco::result<AllocatorSniffer>
    try_allocate(reloco::fallible_allocator &a, const AllocatorSniffer &) {
      captured_alloc = &a;
      return AllocatorSniffer{};
    }
  };

  AllocatorSniffer original;
  auto res = reloco::construction_helpers::try_clone(alloc, original);

  ASSERT_TRUE(res.has_value());
  // Verify that the fallback to try_allocate actually passed our 'alloc' object
  EXPECT_EQ(captured_alloc, &alloc);
}

TEST_F(ConstructionHelpersTest, HandlesTypesThatReturnErrorsFromTryCreate) {
  struct AlwaysFails {
    static reloco::result<AlwaysFails> try_create() {
      return reloco::unexpected(reloco::error::unsupported_operation);
    }
  };

  // This proves that we don't just check if try_create exists,
  // but we actually respect the error it returns.
  auto res = reloco::construction_helpers::try_allocate<AlwaysFails>(alloc);

  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), reloco::error::unsupported_operation);
}

TEST_F(ConstructionHelpersTest, Usage_ManualStackBuffer) {
  struct KernelTask {
    int priority;
    bool active;
    // Two-phase init
    reloco::result<void> try_construct(int p) noexcept {
      if (p < 0)
        return reloco::unexpected(reloco::error::invalid_argument);
      priority = p;
      active = true;
      return {};
    }
  };

  // Allocate raw storage on the stack
  alignas(KernelTask) std::byte task_buffer[sizeof(KernelTask)];
  auto *task_ptr = reinterpret_cast<KernelTask *>(task_buffer);

  auto res = reloco::construction_helpers::try_construct(alloc, task_ptr, 10);

  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(task_ptr->priority, 10);

  task_ptr->~KernelTask();
}

TEST_F(ConstructionHelpersTest, Usage_UniformFactoryPattern) {
  // A complex type requiring an allocator
  struct FileHandle {
    reloco::fallible_allocator *a;
    static reloco::result<FileHandle>
    try_allocate(reloco::fallible_allocator &alloc) {
      return FileHandle{&alloc};
    }
  };

  // A simple type that doesn't
  struct Point {
    int x, y;
  };

  // The API user uses the SAME helper for both
  auto file_res = reloco::construction_helpers::try_allocate<FileHandle>(alloc);
  auto point_res =
      reloco::construction_helpers::try_allocate<Point>(alloc, 10, 20);

  ASSERT_TRUE(file_res.has_value());
  ASSERT_TRUE(point_res.has_value());

  EXPECT_EQ(file_res->a, &alloc);
  EXPECT_EQ(point_res->x, 10);
}

TEST_F(ConstructionHelpersTest, Usage_SafeMoveValidation) {
  struct MoveTracker {
    bool moved = false;
    MoveTracker() = default;
    MoveTracker(MoveTracker &&other) noexcept { other.moved = true; }
  };

  MoveTracker local_obj;

  auto res = reloco::construction_helpers::try_allocate<MoveTracker>(
      alloc, std::move(local_obj));

  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(local_obj.moved);
}

TEST_F(ConstructionHelpersTest, Usage_CrossAllocatorCloning) {
  struct SharedBuffer {
    size_t size;
    int *data;
    reloco::fallible_allocator *alloc;

    // Custom clone logic to ensure new buffer uses the NEW allocator
    reloco::result<SharedBuffer>
    try_clone(reloco::fallible_allocator &new_alloc) const {
      auto new_data = new_alloc.allocate(sizeof(int) * size, alignof(int));
      if (!new_data)
        return reloco::unexpected(reloco::error::allocation_failed);

      std::copy(data, data + size, (int *)new_data->ptr);
      return SharedBuffer{size, (int *)new_data->ptr, &new_alloc};
    }

    ~SharedBuffer() {
      if (alloc)
        alloc->deallocate(data, sizeof(int) * size);
    }
  };

  int raw_data[] = {1, 2, 3};
  SharedBuffer original{3, raw_data, nullptr}; // Simplified for test

  // Clone into a DIFFERENT allocator
  auto clone_res = reloco::construction_helpers::try_clone(alloc, original);

  ASSERT_TRUE(clone_res.has_value());
  EXPECT_EQ(clone_res->alloc, &alloc); // Proves it transitioned pools
  EXPECT_EQ(clone_res->data[0], 1);
}
