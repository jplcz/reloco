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
