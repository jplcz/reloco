#include <gtest/gtest.h>
#include <reloco/string_view.hpp>

TEST(SafeStringViewTest, InheritanceAndAsserts) {
  const char *data = "RELOCO";
  reloco::string_view view(data);

  // Verify standard API still works (Inherited)
  EXPECT_EQ(view.size(), 6);
  EXPECT_TRUE(view.starts_with("RE"));

  // Verify Result-based API
  auto char_res = view.try_at(0);
  ASSERT_TRUE(char_res.has_value());
  EXPECT_EQ(*char_res, 'R');

  auto fail_res = view.try_at(100);
  EXPECT_EQ(fail_res.error(), reloco::error::out_of_bounds);

  // Substring behavior
  auto sub = view.substr(0, 2);
  static_assert(std::is_same_v<decltype(sub), reloco::string_view>,
                "Must return reloco::string_view");
}

TEST(SafeStringViewTest, NullConstructorSafety) {
  // Safe empty construction
  reloco::string_view v1(nullptr);
  EXPECT_TRUE(v1.empty());
  EXPECT_EQ(v1.size(), 0);

  // This would trigger RELOCO_ASSERT in Debug/Release
  ASSERT_DEATH(reloco::string_view v2(nullptr, 10), "");

  // C-String null safety
  const char *n_str = nullptr;
  reloco::string_view v3(n_str);
  EXPECT_TRUE(v3.empty());
}

TEST(BasicStringViewTest, WideStringSupport) {
  reloco::wstring_view wv(L"\\Device\\Harddisk0");

  EXPECT_EQ(wv.size(), 17);
  EXPECT_EQ(wv[0], L'\\');
}

TEST(StringViewFallibleTest, TryAtBoundsChecking) {
  reloco::string_view view("Reloco", 6);

  // Valid index
  auto res_valid = view.try_at(0);
  ASSERT_TRUE(res_valid.has_value());
  EXPECT_EQ(*res_valid, 'R');

  // Edge case: Exactly at size()
  auto res_edge = view.try_at(6);
  ASSERT_FALSE(res_edge.has_value());
  EXPECT_EQ(res_edge.error(), reloco::error::out_of_bounds);

  // Far out of bounds
  auto res_oob = view.try_at(100);
  ASSERT_FALSE(res_oob.has_value());
}

TEST(StringViewFallibleTest, TryDataOnEmptyView) {
  reloco::string_view empty_view;
  reloco::string_view null_view(nullptr);

  // Both should return empty_container error rather than a null pointer
  auto res1 = empty_view.try_data();
  EXPECT_FALSE(res1.has_value());
  EXPECT_EQ(res1.error(), reloco::error::container_empty);

  auto res2 = null_view.try_data();
  EXPECT_FALSE(res2.has_value());

  // Non-empty view should succeed
  reloco::string_view valid("data");
  EXPECT_TRUE(valid.try_data().has_value());
}
TEST(StringViewFallibleTest, TrySubstrLogic) {
  reloco::string_view view("KernelMode", 10);

  // Valid substring
  auto sub_res = view.try_substr(0, 6); // "Kernel"
  ASSERT_TRUE(sub_res.has_value());
  EXPECT_EQ(sub_res->size(), 6);
  EXPECT_EQ((*sub_res)[0], 'K');

  // Position exactly at size (valid, returns empty view)
  auto sub_end = view.try_substr(10);
  ASSERT_TRUE(sub_end.has_value());
  EXPECT_TRUE(sub_end->empty());

  // Position out of bounds
  auto sub_fail = view.try_substr(11);
  ASSERT_FALSE(sub_fail.has_value());
  EXPECT_EQ(sub_fail.error(), reloco::error::out_of_bounds);
}
TEST(StringViewFallibleTest, TryRemovePrefix) {
  reloco::string_view view("PrefixData", 10);

  // Partial removal
  auto res = view.try_remove_prefix(6);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(view, "Data");

  // Removal exceeding size
  auto res_fail = view.try_remove_prefix(5); // Only 4 left
  ASSERT_FALSE(res_fail.has_value());
  EXPECT_EQ(res_fail.error(), reloco::error::out_of_bounds);
  EXPECT_EQ(view, "Data"); // Ensure original view state is preserved on failure
}

TEST(StringViewSafetyTest, ConstructorInvariants) {
  // Proves nullptr results in empty view without crashing
  reloco::string_view v_null(nullptr);
  EXPECT_TRUE(v_null.empty());
  EXPECT_EQ(v_null.size(), 0);

  // Proves empty string results in empty view
  reloco::string_view v_empty("");
  EXPECT_TRUE(v_empty.empty());

  // Proves valid pointer + length works
  const char *buf = "test";
  reloco::string_view v_buf(buf, 4);
  EXPECT_EQ(v_buf.data(), buf);
  EXPECT_EQ(v_buf.size(), 4);
}

TEST(StringViewFallibleTest, MonadicUsage) {
  reloco::string_view path("/device/harddisk0/partition1");

  // Simulate a safe "get drive index" operation
  auto get_drive = [](reloco::string_view v) -> reloco::result<char> {
    return v
        .try_substr(16) // Get "0/partition1"
        .and_then([](reloco::string_view s) {
          return s.try_at(0); // Get '0'
        });
  };

  auto res = get_drive(path);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, '0');

  // Test with a path that is too short
  auto short_res = get_drive("/device/short");
  EXPECT_FALSE(short_res.has_value());
}
