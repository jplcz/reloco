#include <gtest/gtest.h>
#include <reloco/allocator.hpp>
#include <reloco/fallible_constructed.hpp>
#include <type_traits>

namespace {

class ImmovableResource {
public:
  using reloco_fallible_t = void;

  ImmovableResource(const ImmovableResource &) = delete;
  ImmovableResource(ImmovableResource &&) = delete;
  ~ImmovableResource() = default;

  ImmovableResource(reloco::detail::constructor_key<ImmovableResource>) {}

  reloco::result<void>
  try_init(reloco::detail::constructor_key<ImmovableResource>) {
    init_called = true;
    return {};
  }

  bool init_called = false;
};

class MovableResource {
public:
  using reloco_fallible_t = void;

  MovableResource(MovableResource &&other) noexcept : val(other.val) {
    other.val = 0;
  }
  MovableResource &operator=(MovableResource &&other) noexcept {
    val = other.val;
    other.val = 0;
    return *this;
  }

  MovableResource(reloco::detail::constructor_key<MovableResource>)
      : val(100) {}
  reloco::result<void>
  try_init(reloco::detail::constructor_key<MovableResource>) {
    return {};
  }

  int val = 0;
};
class StaticSpy {
public:
  using reloco_fallible_t = void;

  // Static counters to track global state across the test
  static inline int constructor_calls = 0;
  static inline int destructor_calls = 0;
  static inline int init_calls = 0;

  static void reset() {
    constructor_calls = 0;
    destructor_calls = 0;
    init_calls = 0;
  }

  StaticSpy(reloco::detail::constructor_key<StaticSpy>) { constructor_calls++; }

  ~StaticSpy() { destructor_calls++; }

  reloco::result<void> try_init(reloco::detail::constructor_key<StaticSpy>) {
    init_calls++;
    return {};
  }

protected:
  StaticSpy() {}
};

class FailingStaticSpy : public StaticSpy {
public:
  using reloco_fallible_t = void;
  FailingStaticSpy(reloco::detail::constructor_key<FailingStaticSpy> k)
      : StaticSpy() {}

  reloco::result<void>
  try_init(reloco::detail::constructor_key<FailingStaticSpy>) {
    return std::unexpected(reloco::error::already_exists);
  }
};

} // namespace

template <>
struct reloco::is_relocatable<ImmovableResource> : std::false_type {};
template <> struct reloco::is_relocatable<MovableResource> : std::true_type {};

TEST(FallibleConstructedTest, FallibleConstructedBasic) {
  reloco::fallible_constructed<ImmovableResource> wrapper;

  // Asserting getters should trap here if RELOCO_DISABLE_ASSERT is off
  EXPECT_FALSE(wrapper.try_get().has_value());

  // Successful activation
  auto res = wrapper.try_init();
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(wrapper->init_called);
}

TEST(FallibleConstructedTest, FallibleAllocatedMove) {
  reloco::core_allocator alloc;

  reloco::fallible_allocated<ImmovableResource> m1(alloc);
  ASSERT_TRUE(m1.try_init().has_value());

  ImmovableResource *addr = m1.get();

  // Ownership transfer
  reloco::fallible_allocated<ImmovableResource> m2 = std::move(m1);

  EXPECT_EQ(m2.get(), addr); // Address is stable (Safe for Lockdep)
  EXPECT_EQ(m1.try_get().error(), reloco::error::not_initialized);
}

TEST(FallibleConstructedTest, RelocatabilityInheritance) {
  static_assert(!reloco::is_relocatable<
                    reloco::fallible_constructed<ImmovableResource>>::value,
                "Wrapper for immovable type must be immovable");

  static_assert(reloco::is_relocatable<
                    reloco::fallible_constructed<MovableResource>>::value,
                "Wrapper for movable type should be relocatable");

  static_assert(reloco::is_relocatable<
                    reloco::fallible_allocated<ImmovableResource>>::value,
                "Heap manager is always relocatable");
}

TEST(FallibleConstructedTest, ConditionalMoveSemantics) {
  static_assert(!std::is_move_constructible_v<
                reloco::fallible_constructed<ImmovableResource>>);
  static_assert(std::is_move_constructible_v<
                reloco::fallible_constructed<MovableResource>>);
}

TEST(FallibleConstructedTest, AccessUninitializedConstructed) {
  reloco::fallible_constructed<ImmovableResource> wrapper;

  auto res = wrapper.try_get();
  EXPECT_EQ(res.error(), reloco::error::not_initialized);

  EXPECT_DEATH({ (void)wrapper->init_called; }, "");
}

TEST(StaticFallibleTest, DestructorIsNeverCalled) {
  StaticSpy::reset();

  {
    reloco::static_fallible_constructed<StaticSpy> wrapper;
    ASSERT_TRUE(wrapper.try_init().has_value());

    EXPECT_EQ(StaticSpy::constructor_calls, 1);
    EXPECT_EQ(StaticSpy::init_calls, 1);
    EXPECT_EQ(StaticSpy::destructor_calls, 0);
  } // Wrapper goes out of scope here

  EXPECT_EQ(StaticSpy::destructor_calls, 0)
      << "Destructor was called, but static_fallible_constructed must "
         "eliminate it!";
}

TEST(StaticFallibleTest, CleansUpOnlyOnFailure) {
  StaticSpy::reset();

  {
    reloco::static_fallible_constructed<FailingStaticSpy> wrapper;
    auto res = wrapper.try_init();
    EXPECT_FALSE(res.has_value());

    // On failure, it SHOULD call the destructor to avoid leaving garbage
    EXPECT_EQ(StaticSpy::destructor_calls, 1);
  }
  EXPECT_EQ(StaticSpy::destructor_calls, 1);
}

TEST(StaticFallibleTest, AlignmentAndBufferSafety) {
  struct alignas(64) AlignedType {
    using reloco_fallible_t = void;
    char data[64];
    AlignedType(reloco::detail::constructor_key<AlignedType>) {}
    reloco::result<void>
    try_init(reloco::detail::constructor_key<AlignedType>) {
      return {};
    }
  };

  reloco::static_fallible_constructed<AlignedType> wrapper;
  ASSERT_TRUE(wrapper.try_init().has_value());

  uintptr_t addr = reinterpret_cast<uintptr_t>(wrapper.get());
  EXPECT_EQ(addr % 64, 0) << "Buffer was not aligned to 64 bytes!";
}
