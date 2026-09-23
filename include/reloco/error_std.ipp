// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file error_std.ipp @brief Out-of-line bodies for detail::error_category_impl
 * and the reloco::error_category()/make_error_code()/make_error_condition()
 * free functions (see error_std.hpp). Included from error_std.hpp itself,
 * guarded on RELOCO_SHARED_PROVIDE_DEFINITIONS (see reloco/detail/compat.hpp).
 * Never included directly.
 *
 * Deliberately never pulled in by reloco_compile.hpp's default umbrella:
 * error_std.hpp itself is an opt-in bridge to <system_error>, and
 * error_category_impl::message() below allocates a std::string per call --
 * exactly the kind of std-allocating operation reloco's own core stays free
 * of. A RELOCO_SHARED_BUILD translation unit that wants this migrated too
 * must #include <reloco/error_std.hpp> itself, alongside reloco_compile.hpp
 * (see docs/shared-library.md).
 */

namespace detail {

RELOCO_API std::string error_category_impl::message(int ev) const {
  switch (static_cast<error>(ev)) {
  case error::allocation_failed:
    return "the allocator failed to provide/grow/shrink a memory block";
  case error::in_place_growth_failed:
    return "expand_in_place could not grow a block without moving it";
  case error::unsupported_operation:
    return "the operation is not supported by this concrete type/backend";
  case error::out_of_range:
    return "a value fell outside the range required by the operation";
  case error::invalid_argument:
    return "an argument failed a precondition check";
  case error::already_exists:
    return "an equivalent key/element is already present";
  case error::empty_pointer:
    return "a smart pointer was empty when a non-empty one was required";
  case error::pointer_expired:
    return "the last owning shared_ptr has already released the object";
  case error::no_owner:
    return "an operation requiring an owning handle was attempted on a non-owning one";
  case error::out_of_bounds:
    return "a container index/iterator fell outside its valid range";
  case error::deadlock:
    return "a locking operation detected it would deadlock";
  case error::invalid_owner:
    return "the caller does not own the resource it is trying to operate on";
  case error::still_locked:
    return "an operation requiring an unlocked resource found it still locked";
  case error::not_locked:
    return "an operation requiring a locked resource found it was not locked";
  case error::timed_out:
    return "a bounded-wait operation did not complete within its deadline";
  case error::try_again:
    return "the operation could not complete right now but may succeed if retried";
  case error::not_initialized:
    return "the object/subsystem was used before its required initialization step";
  case error::container_empty:
    return "an operation requiring at least one element was called on an empty container";
  case error::not_found:
    return "a lookup found no matching key/element";
  case error::integer_overflow:
    return "an arithmetic computation would overflow its integer type";
  case error::division_by_zero:
    return "a division or remainder operation was attempted with a zero divisor";
  case error::capacity_exceeded:
    return "a fixed-capacity container has no room left for another element";
  case error::invalid_state:
    return "the operation is not valid given the object's current state";
  case error::permission_denied:
    return "an OS- or allocator-level access-control check failed";
  case error::interrupted:
    return "the underlying operation was interrupted and may be safely retried";
  case error::resource_exhausted:
    return "a system-imposed resource limit unrelated to heap memory was reached";
  case error::busy:
    return "the resource is currently in use by someone else";
  case error::io_error:
    return "a lower-level I/O operation failed";
  case error::operation_canceled:
    return "the operation was explicitly canceled before it could complete";
  }
  return "unknown reloco::error";
}

RELOCO_API std::error_condition error_category_impl::default_error_condition(int ev) const noexcept {
  switch (static_cast<error>(ev)) {
  case error::allocation_failed:
    return std::make_error_condition(std::errc::not_enough_memory);
  case error::invalid_argument:
    return std::make_error_condition(std::errc::invalid_argument);
  case error::out_of_range:
  case error::out_of_bounds:
    return std::make_error_condition(std::errc::result_out_of_range);
  case error::already_exists:
    return std::make_error_condition(std::errc::file_exists);
  case error::deadlock:
    return std::make_error_condition(std::errc::resource_deadlock_would_occur);
  case error::timed_out:
    return std::make_error_condition(std::errc::timed_out);
  case error::try_again:
    return std::make_error_condition(std::errc::resource_unavailable_try_again);
  case error::unsupported_operation:
    return std::make_error_condition(std::errc::operation_not_supported);
  case error::capacity_exceeded:
    return std::make_error_condition(std::errc::no_buffer_space);
  case error::permission_denied:
    return std::make_error_condition(std::errc::permission_denied);
  case error::interrupted:
    return std::make_error_condition(std::errc::interrupted);
  case error::busy:
    return std::make_error_condition(std::errc::device_or_resource_busy);
  case error::io_error:
    return std::make_error_condition(std::errc::io_error);
  case error::operation_canceled:
    return std::make_error_condition(std::errc::operation_canceled);
  case error::integer_overflow:
    return std::make_error_condition(std::errc::value_too_large);
  case error::division_by_zero:
    return std::make_error_condition(std::errc::argument_out_of_domain);
  case error::in_place_growth_failed:
  case error::empty_pointer:
  case error::pointer_expired:
  case error::no_owner:
  case error::invalid_owner:
  case error::still_locked:
  case error::not_locked:
  case error::not_initialized:
  case error::container_empty:
  case error::not_found:
  case error::invalid_state:
  case error::resource_exhausted:
    break;
  }
  return std::error_category::default_error_condition(ev);
}

RELOCO_API bool error_category_impl::same_name(const std::error_category &other) const noexcept {
  return std::string_view(other.name()) == std::string_view(name());
}

RELOCO_API bool error_category_impl::equivalent(const std::error_code &code, int condition) const noexcept {
  if (*this == code.category())
    return code.value() == condition;
  if (same_name(code.category()))
    return code.value() == condition;
  return default_error_condition(condition) == std::error_condition(code.value(), code.category());
}

RELOCO_API bool error_category_impl::equivalent(int code, const std::error_condition &condition) const noexcept {
  if (default_error_condition(code) == condition)
    return true;
  if (same_name(condition.category()))
    return code == condition.value();
  return false;
}

} // namespace detail

RELOCO_API const std::error_category &error_category() noexcept {
  static const detail::error_category_impl instance;
  return instance;
}

RELOCO_API std::error_code make_error_code(error e) noexcept { return {static_cast<int>(e), error_category()}; }

RELOCO_API std::error_condition make_error_condition(error e) noexcept {
  return {static_cast<int>(e), error_category()};
}
