// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <reloco/error_errno.hpp>

#include <cerrno>

using reloco::error;
using reloco::to_errno;

TEST(ErrorErrnoTest, MapsCommonMembersToExpectedErrno) {
  EXPECT_EQ(to_errno(error::allocation_failed), ENOMEM);
  EXPECT_EQ(to_errno(error::invalid_argument), EINVAL);
  EXPECT_EQ(to_errno(error::out_of_range), ERANGE);
  EXPECT_EQ(to_errno(error::out_of_bounds), ERANGE);
  EXPECT_EQ(to_errno(error::already_exists), EEXIST);
  EXPECT_EQ(to_errno(error::deadlock), EDEADLK);
  EXPECT_EQ(to_errno(error::timed_out), ETIMEDOUT);
  EXPECT_EQ(to_errno(error::try_again), EAGAIN);
  EXPECT_EQ(to_errno(error::unsupported_operation), ENOTSUP);
  EXPECT_EQ(to_errno(error::capacity_exceeded), ENOBUFS);
  EXPECT_EQ(to_errno(error::permission_denied), EACCES);
  EXPECT_EQ(to_errno(error::security_violation), EACCES);
  EXPECT_EQ(to_errno(error::interrupted), EINTR);
  EXPECT_EQ(to_errno(error::busy), EBUSY);
  EXPECT_EQ(to_errno(error::still_locked), EBUSY);
  EXPECT_EQ(to_errno(error::io_error), EIO);
  EXPECT_EQ(to_errno(error::operation_canceled), ECANCELED);
  EXPECT_EQ(to_errno(error::integer_overflow), EOVERFLOW);
  EXPECT_EQ(to_errno(error::division_by_zero), EDOM);
  EXPECT_EQ(to_errno(error::not_found), ENOENT);
  EXPECT_EQ(to_errno(error::page_fault), EFAULT);
  EXPECT_EQ(to_errno(error::pointer_expired), ESTALE);
  EXPECT_EQ(to_errno(error::no_owner), EPERM);
  EXPECT_EQ(to_errno(error::invalid_owner), EPERM);
  EXPECT_EQ(to_errno(error::resource_exhausted), EAGAIN);
}

TEST(ErrorErrnoTest, ApproximatedMembersFallBackToInvalidArgument) {
  EXPECT_EQ(to_errno(error::empty_pointer), EINVAL);
  EXPECT_EQ(to_errno(error::not_locked), EINVAL);
  EXPECT_EQ(to_errno(error::not_initialized), EINVAL);
  EXPECT_EQ(to_errno(error::invalid_state), EINVAL);
  EXPECT_EQ(to_errno(error::container_empty), ENOENT);
  EXPECT_EQ(to_errno(error::in_place_growth_failed), ENOMEM);
}

TEST(ErrorErrnoTest, IsConstexprEvaluable) {
  constexpr int mapped = to_errno(error::invalid_argument);
  static_assert(mapped == EINVAL, "to_errno must be usable in a constant expression");
  EXPECT_EQ(mapped, EINVAL);
}

TEST(ErrorErrnoTest, EveryEnumeratorMapsToANonZeroErrno) {
  constexpr error all_errors[] = {
      error::allocation_failed,
      error::in_place_growth_failed,
      error::unsupported_operation,
      error::out_of_range,
      error::invalid_argument,
      error::already_exists,
      error::empty_pointer,
      error::pointer_expired,
      error::no_owner,
      error::out_of_bounds,
      error::deadlock,
      error::invalid_owner,
      error::still_locked,
      error::not_locked,
      error::timed_out,
      error::try_again,
      error::not_initialized,
      error::container_empty,
      error::not_found,
      error::integer_overflow,
      error::division_by_zero,
      error::capacity_exceeded,
      error::invalid_state,
      error::permission_denied,
      error::interrupted,
      error::resource_exhausted,
      error::busy,
      error::io_error,
      error::operation_canceled,
      error::security_violation,
      error::page_fault,
  };

  for (error e : all_errors) {
    EXPECT_NE(to_errno(e), 0) << "error enumerator " << static_cast<int>(e) << " mapped to errno 0";
  }
}
