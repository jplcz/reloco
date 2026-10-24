// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// Dependency-free so that both `span.hpp` and `string_view.hpp` can use it without including each other.

#include <cstddef>
#include <type_traits>

namespace reloco {

/**
 * @brief True for the one-byte character/integer types real code uses for raw memory: `char`, `signed char`,
 * `unsigned char` (`uint8_t`), `char8_t` and `std::byte` (cv-qualifiers ignored).
 */
template <typename T>
inline constexpr bool is_byte_like_v = sizeof(T) == 1 && (std::is_same_v<std::remove_cv_t<T>, char> ||
                                                           std::is_same_v<std::remove_cv_t<T>, signed char> ||
                                                           std::is_same_v<std::remove_cv_t<T>, unsigned char> ||
#if defined(__cpp_char8_t)
                                                           std::is_same_v<std::remove_cv_t<T>, char8_t> ||
#endif
                                                           std::is_same_v<std::remove_cv_t<T>, std::byte>);

} // namespace reloco
