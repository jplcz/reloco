// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Compiles every public reloco header under each supported C++ standard.
// Used by RELOCO_BUILD_HEADER_CHECKS and scripts/check-unsafe-buffer-usage.sh.

#include <reloco/allocator.hpp>
#include <reloco/any.hpp>
#include <reloco/array.hpp>
#include <reloco/bytes.hpp>
#include <reloco/channel.hpp>
#include <reloco/checked.hpp>
#include <reloco/checked_value.hpp>
#include <reloco/collection_view.hpp>
#include <reloco/concepts.hpp>
#include <reloco/construction_helpers.hpp>
#include <reloco/container_ref.hpp>
#include <reloco/container_ref_std.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/duration.hpp>
#include <reloco/error.hpp>
#include <reloco/error_std.hpp>
#include <reloco/expected.hpp>
#include <reloco/flat_hash_map.hpp>
#include <reloco/flat_hash_set.hpp>
#include <reloco/flat_map.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/function.hpp>
#include <reloco/function_ref.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/inline_flat_map.hpp>
#include <reloco/inline_flat_set.hpp>
#include <reloco/inline_string.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/inplace_function.hpp>
#include <reloco/int_ops.hpp>
#include <reloco/lifetime.hpp>
#include <reloco/once_lock.hpp>
#include <reloco/optional.hpp>
#include <reloco/park.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/rvalue_safety.hpp>
#include <reloco/scope.hpp>
#include <reloco/send_sync.hpp>
#include <reloco/shared_ptr.hpp>
#include <reloco/span.hpp>
#include <reloco/stack_allocator.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>
#include <reloco/thread.hpp>
#include <reloco/tls_provider.hpp>
#include <reloco/tree_map.hpp>
#include <reloco/tree_set.hpp>
#include <reloco/type_id.hpp>
#include <reloco/unique_ptr.hpp>
#include <reloco/value_ptr.hpp>
#include <reloco/value_ref.hpp>
#include <reloco/vector.hpp>

#include <reloco/detail/assert.hpp>
#include <reloco/detail/compat.hpp>
#include <reloco/detail/flat_container_base.hpp>
#include <reloco/detail/sanitizer.hpp>
