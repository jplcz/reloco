// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file node_base.hpp
 * @brief `node_base`: the single-allocation node layout shared by every
 * future type-erased node-based container engine (binary search tree
 * today; potentially a node-based list/hash bucket tomorrow).
 *
 * Each node is one allocator block laid out as `[header][payload]`, header
 * first: `node_header` is the node's structural bookkeeping (`parent`/
 * `left`/`right` links, plus one reserved byte for a balancing policy's own
 * use -- a red-black colour bit, an AVL balance factor, ... -- unused by a
 * plain, unbalanced binary search tree). Unlike `std::allocate_shared`'s
 * control block, `node_header`'s own alignment is *not* boosted to match
 * the payload's -- it stays a plain, fixed-size, fixed-alignment struct, and
 * `node_base` computes the header-to-payload offset itself from a runtime
 * `type_metadata`, exactly like `type_operations`/`vector_operations`
 * already resolve per-`T` facts through `metadata_for<T>` rather than a
 * template parameter: templating this class on the payload's alignment
 * would instantiate a fresh `node_base<Alignment>` (and every method on
 * it) per distinct alignment value used anywhere in the program, for a
 * saving that only replaces one `(value + mask) & ~mask` round-up (cheap,
 * branch-free, and already exactly how `try_reserve_base` et al. compute
 * their own byte offsets) with a compile-time constant.
 *
 * `header_of()`/`payload_of()` convert between the two ends of that single
 * allocation via that computed offset -- `header_of(payload, type) ==
 * static_cast<std::byte *>(payload) - payload_offset(type)` -- and both go
 * through `std::launder()`, since the byte range they reinterpret was
 * placement-new'd as one type (`node_header` or the payload's `T`) and is
 * being viewed through a pointer of the *other* type; without laundering,
 * the compiler would be entitled to assume the two pointers can never
 * alias the same storage.
 *
 * This header only provides the node's memory layout and raw
 * allocate/deallocate of one node's storage -- it does not construct or
 * destroy the payload itself (that is `type_operations`' job, applied to
 * `payload_of(node, type)` by the engine that owns a `node_base`).
 */

#include "../allocator.hpp"
#include "../error.hpp"
#include "../expected.hpp"
#include "../reloco_extern.hpp"
#include "type_metadata.hpp"

#include <cstddef>
#include <new>

namespace reloco::detail {

/**
 * @brief One node's structural bookkeeping: links to its parent and both
 * children, plus one reserved byte a balancing policy (red-black colour,
 * AVL balance factor, ...) can use for its own purposes -- unused by a
 * plain, unbalanced binary search tree.
 */
struct node_header {
  node_header *parent = nullptr;
  node_header *left = nullptr;
  node_header *right = nullptr;
  unsigned char aux = 0;
};

/**
 * @brief Computes the single-allocation `[header][payload]` node layout
 * for a payload described by a runtime `type_metadata`, and
 * allocates/deallocates that one block through an `allocator_ref`. See the
 * file-level comment for the layout and offset-computation rationale.
 */
class RELOCO_EXPORT node_base {
public:
  using header = node_header;

  /// @brief Bytes occupied by the header itself, before any payload
  /// alignment padding.
  static constexpr std::size_t header_size = sizeof(header);

  /// @brief The header's own natural alignment (that of three pointers).
  static constexpr std::size_t header_alignment = alignof(header);

  /// @brief Bytes from the start of the node allocation to the payload:
  /// `header_size` rounded up to @p type's alignment, so the payload
  /// -- placed at that offset from a block itself allocated at
  /// `node_alignment(type)` -- always lands correctly aligned.
  [[nodiscard]] static constexpr std::size_t payload_offset(const type_metadata &type) noexcept {
    const std::size_t mask = type.element_alignment - 1;
    return (header_size + mask) & ~mask;
  }

  /// @brief Alignment the whole node block must be allocated at: never
  /// weaker than the header's own alignment, even for a sub-pointer-aligned
  /// payload (e.g. `char`).
  [[nodiscard]] static constexpr std::size_t node_alignment(const type_metadata &type) noexcept {
    return type.element_alignment > header_alignment ? type.element_alignment : header_alignment;
  }

  /// @brief Total bytes to allocate for one node given the payload's
  /// runtime metadata.
  [[nodiscard]] static constexpr std::size_t node_size(const type_metadata &type) noexcept {
    return payload_offset(type) + type.element_size;
  }

  /// @brief The payload storage belonging to @p node -- uninitialized
  /// bytes until the caller constructs a `T` there via `type_operations`.
  [[nodiscard]] static void *payload_of(header *node, const type_metadata &type) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return std::launder(reinterpret_cast<std::byte *>(node) + payload_offset(type));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  /// @brief `const`-qualified overload of `payload_of(header *, const type_metadata &)`.
  [[nodiscard]] static const void *payload_of(const header *node, const type_metadata &type) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return std::launder(reinterpret_cast<const std::byte *>(node) + payload_offset(type));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  /// @brief Recovers the node header from a pointer to its payload, i.e.
  /// the inverse of `payload_of()`: `header_of(payload_of(node, type),
  /// type) == node`.
  [[nodiscard]] static header *header_of(void *payload, const type_metadata &type) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return std::launder(reinterpret_cast<header *>(static_cast<std::byte *>(payload) - payload_offset(type)));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  /// @brief `const`-qualified overload of `header_of(void *, const type_metadata &)`.
  [[nodiscard]] static const header *header_of(const void *payload, const type_metadata &type) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    return std::launder(
        reinterpret_cast<const header *>(static_cast<const std::byte *>(payload) - payload_offset(type)));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }

  /**
   * @brief Allocates one node's storage (header + uninitialized payload
   * bytes) and default-constructs the header in place (`parent`/`left`/
   * `right` null, `aux` zero). The payload is left uninitialized -- the
   * caller constructs it via `type_operations::copy_construct_one`/
   * `clone_one` at `payload_of(*result, type)`.
   */
  [[nodiscard]] static result<header *> try_allocate_node(allocator_ref alloc, const type_metadata &type) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    auto block = alloc.allocate(node_size(type), node_alignment(type));
    RELOCO_END_UNSAFE_BUFFER_USAGE
    if (!block)
      return unexpected(block.error());
    return ::new (block->ptr) header();
  }

  /**
   * @brief Destroys the header and frees @p node's storage. The caller
   * must have already destroyed the payload (via
   * `type_operations::destroy_one` at `payload_of(node, type)`) before
   * calling this.
   */
  static void deallocate_node(allocator_ref alloc, header *node, const type_metadata &type) noexcept {
    node->~header();
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    alloc.deallocate(node, node_size(type));
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }
};

} // namespace reloco::detail
