// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/error_std.hpp>

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

TEST(ErrorStdTest, ImplicitlyConvertsToErrorCode) {
  std::error_code ec = reloco::error::not_found;

  EXPECT_EQ(ec.category().name(), std::string("reloco"));
  EXPECT_EQ(ec.value(), static_cast<int>(reloco::error::not_found));
  EXPECT_FALSE(ec.message().empty());
}

TEST(ErrorStdTest, ComparesEqualAcrossIdenticalEnumerators) {
  std::error_code lhs = reloco::error::allocation_failed;
  std::error_code rhs = reloco::error::allocation_failed;

  EXPECT_EQ(lhs, rhs);
  EXPECT_NE(lhs, std::error_code(reloco::error::not_found));
}

TEST(ErrorStdTest, MakeErrorCodeUsesTheSameSingletonCategory) {
  EXPECT_EQ(&reloco::make_error_code(reloco::error::busy).category(), &reloco::error_category());
}

TEST(ErrorStdTest, EveryEnumeratorHasADistinctNonEmptyMessage) {
  constexpr reloco::error all_errors[] = {
      reloco::error::allocation_failed,
      reloco::error::in_place_growth_failed,
      reloco::error::unsupported_operation,
      reloco::error::out_of_range,
      reloco::error::invalid_argument,
      reloco::error::already_exists,
      reloco::error::empty_pointer,
      reloco::error::pointer_expired,
      reloco::error::no_owner,
      reloco::error::out_of_bounds,
      reloco::error::deadlock,
      reloco::error::invalid_owner,
      reloco::error::still_locked,
      reloco::error::not_locked,
      reloco::error::timed_out,
      reloco::error::try_again,
      reloco::error::not_initialized,
      reloco::error::container_empty,
      reloco::error::not_found,
      reloco::error::integer_overflow,
      reloco::error::capacity_exceeded,
      reloco::error::invalid_state,
      reloco::error::permission_denied,
      reloco::error::interrupted,
      reloco::error::resource_exhausted,
      reloco::error::busy,
      reloco::error::io_error,
      reloco::error::operation_canceled,
  };

  std::vector<std::string> messages;
  for (auto e : all_errors) {
    const std::string message = reloco::error_category().message(static_cast<int>(e));
    EXPECT_FALSE(message.empty());
    EXPECT_NE(message, "unknown reloco::error");
    messages.push_back(message);
  }

  std::sort(messages.begin(), messages.end());
  EXPECT_EQ(std::adjacent_find(messages.begin(), messages.end()), messages.end())
      << "two reloco::error members share the exact same message() text";
}

TEST(ErrorStdTest, UnknownValueFallsBackToAGenericMessage) {
  const auto message = reloco::error_category().message(0);
  EXPECT_EQ(message, "unknown reloco::error");
}

TEST(ErrorStdTest, ThrowsAndCatchesAsSystemError) {
  try {
    throw std::system_error(reloco::error::deadlock);
  } catch (const std::system_error &ex) {
    EXPECT_EQ(ex.code(), std::error_code(reloco::error::deadlock));
    return;
  }
  FAIL() << "expected std::system_error to be thrown";
}

TEST(ErrorStdTest, ImplicitlyConvertsToErrorCondition) {
  std::error_condition ec = reloco::error::not_found;

  EXPECT_EQ(ec.category().name(), std::string("reloco"));
  EXPECT_EQ(ec.value(), static_cast<int>(reloco::error::not_found));
}

TEST(ErrorStdTest, ErrorCodeComparesEqualToMatchingErrorCondition) {
  const std::error_code ec = reloco::error::busy;

  EXPECT_EQ(ec, std::error_condition(reloco::error::busy));
  EXPECT_EQ(ec, std::error_code(reloco::error::busy));
  EXPECT_NE(ec, std::error_condition(reloco::error::io_error));
}

TEST(ErrorStdTest, ErrorCodeComparesEqualToTheEquivalentPosixCondition) {
  EXPECT_EQ(std::error_code(reloco::error::timed_out), std::errc::timed_out);
  EXPECT_EQ(std::error_code(reloco::error::busy), std::errc::device_or_resource_busy);
  EXPECT_EQ(std::error_code(reloco::error::allocation_failed), std::errc::not_enough_memory);
  EXPECT_EQ(std::error_code(reloco::error::invalid_argument), std::errc::invalid_argument);
  EXPECT_EQ(std::error_code(reloco::error::already_exists), std::errc::file_exists);
  EXPECT_EQ(std::error_code(reloco::error::interrupted), std::errc::interrupted);
  EXPECT_NE(std::error_code(reloco::error::busy), std::errc::timed_out);
}

TEST(ErrorStdTest, ErrorCodeComparesEqualAgainstAGenericCategoryErrnoCode) {
  // Simulates a `std::error_code` built from a POSIX `errno` value (as
  // `std::generic_category()`-based codes typically are), confirming
  // `reloco::error` is plugged into that same POSIX bridge from either
  // side of the `error_code == error_condition` comparison. (Plain
  // `error_code == error_code` across two unrelated categories is, per
  // the standard, never bridgeable -- see the file-level docs.)
  const std::error_code errno_code(static_cast<int>(std::errc::device_or_resource_busy), std::generic_category());

  EXPECT_EQ(errno_code, std::error_condition(reloco::error::busy));
  EXPECT_EQ(std::error_condition(reloco::error::busy), errno_code);
}

TEST(ErrorStdTest, MembersWithNoPosixEquivalentOnlyMatchThemselves) {
  const std::error_condition condition =
      reloco::error_category().default_error_condition(static_cast<int>(reloco::error::not_found));

  EXPECT_EQ(condition, std::error_condition(reloco::error::not_found));
  EXPECT_EQ(&condition.category(), &reloco::error_category());
}

TEST(ErrorStdTest, EquivalentMatchesByCategoryNameNotIdentity) {
  // Simulates the cross-DSO scenario documented in error_std.hpp: a second,
  // distinct `error_category` instance whose `name()` also reports
  // "reloco" but which is a completely different object/type from the
  // real `detail::error_category_impl`.
  class other_reloco_category final : public std::error_category {
  public:
    [[nodiscard]] const char *name() const noexcept override { return "reloco"; }
    [[nodiscard]] std::string message(int) const override { return "other"; }
  } other_category;

  const std::error_code foreign_code(static_cast<int>(reloco::error::busy), other_category);
  const std::error_condition ours(reloco::error::busy);

  EXPECT_TRUE(reloco::error_category().equivalent(foreign_code, static_cast<int>(reloco::error::busy)));
  EXPECT_EQ(foreign_code, ours);

  const std::error_condition foreign_condition(static_cast<int>(reloco::error::busy), other_category);
  EXPECT_TRUE(reloco::error_category().equivalent(static_cast<int>(reloco::error::busy), foreign_condition));

  const std::error_code unrelated_code(static_cast<int>(reloco::error::busy), std::generic_category());
  EXPECT_FALSE(reloco::error_category().equivalent(unrelated_code, static_cast<int>(reloco::error::busy)));
}
