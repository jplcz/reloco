// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/string_view.hpp>

#include <string>
#include <string_view>
#include <type_traits>

static_assert(!std::is_constructible_v<reloco::string_view, std::string &&>,
              "reloco::string_view must reject temporary owning strings");

TEST(StringViewTest, InteroperatesWithStandardStringViews) {
  const std::string storage = "interop";
  const std::string_view standard = storage;
  const reloco::string_view custom = standard;
  const std::string_view roundtrip = custom;

  EXPECT_EQ(custom, "interop");
  EXPECT_EQ(roundtrip, standard);
  EXPECT_EQ(reloco::string_view(storage), custom);
  EXPECT_EQ(std::hash<reloco::string_view>{}(custom), std::hash<std::string_view>{}(standard));
}

TEST(StringViewTest, ProvidesCheckedOperations) {
  reloco::string_view view = "value";

  ASSERT_TRUE(view.try_front().has_value());
  EXPECT_EQ(view.try_front().value().get(), 'v');
  ASSERT_TRUE(view.try_substr(1, 3).has_value());
  EXPECT_EQ(view.try_substr(1, 3).value(), "alu");
  EXPECT_FALSE(view.try_at(view.size()).has_value());
}

TEST(StringViewTest, ProvidesExplicitlyUnsafeAccessors) {
  reloco::string_view view = "value";

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
  EXPECT_EQ(view.unsafe_front(), 'v');
  EXPECT_EQ(view.unsafe_back(), 'e');
  EXPECT_EQ(view.unsafe_substr(1, 3), "alu");
  EXPECT_EQ(view.unsafe_data(), view.data());
  RELOCO_END_UNSAFE_BUFFER_USAGE
}
