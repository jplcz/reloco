// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file type_metadata.hpp
 * @brief `type_metadata`: the compile-time facts about a type `T` that
 * every type-erased reloco container engine (`vector_base.hpp` today;
 * a future map/list/node-based engine tomorrow) needs in order to operate
 * on `T` through `void*` without a template parameter of its own.
 *
 * `type_metadata` is deliberately container-shape-agnostic: it only
 * describes `T` itself (size, alignment, and the handful of triviality
 * facts `construction_helpers.hpp` already tiers dispatch on), never
 * anything about how a particular container lays those `T`s out in memory
 * (contiguous array, node, bucket, ...). That split is what lets it be
 * shared as-is by a type-erased `vector_operations`-style table today and
 * an analogous per-node or per-bucket table for a future type-erased
 * map/list/flat-container engine, without those engines needing to agree
 * on anything beyond "here is a `const type_metadata &` describing my
 * element type".
 *
 * `metadata_for<T>` computes the value once per `T` as a `constexpr`
 * variable template, so callers pass `metadata_for<T>` into a type-erased
 * API instead of recomputing these facts themselves.
 */

#include "../alignment.hpp"
#include "../reloco_extern.hpp"
#include "../relocatable.hpp"

#include <cstddef>
#include <type_traits>

namespace reloco::detail {

/**
 * @brief Compile-time facts about `T` needed by reloco's type-erased
 * container engines, captured once per `T` in `metadata_for<T>` so the
 * untyped engine classes that consume it never need a template parameter
 * themselves.
 */
struct RELOCO_EXPORT type_metadata {
  std::size_t element_size;
  std::size_t element_alignment;
  bool is_trivially_destructible;
  bool is_trivially_relocatable;
  bool is_trivially_copyable;
  bool is_default_constructible;
};

/**
 * @brief The `type_metadata` for `T`, computed once as a `constexpr` value.
 */
template <typename T>
inline constexpr type_metadata metadata_for = {sizeof(T),
                                               effective_alignment_v<T>,
                                               std::is_trivially_destructible_v<T>,
                                               is_trivially_relocatable_v<T>,
                                               std::is_trivially_copyable_v<T>,
                                               std::is_default_constructible_v<T>};

} // namespace reloco::detail
