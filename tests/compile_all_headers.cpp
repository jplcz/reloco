// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Compiles every public reloco header under each supported C++ standard.
// Used by RELOCO_BUILD_HEADER_CHECKS and scripts/check-unsafe-buffer-usage.sh.

#include <reloco/allocator.hpp>
#include <reloco/any.hpp>
#include <reloco/array.hpp>
#include <reloco/atomic_ops.hpp>
#include <reloco/barrier.hpp>
#include <reloco/bucket_allocator.hpp>
#include <reloco/bucket_growth.hpp>
#include <reloco/bytes.hpp>
#include <reloco/channel.hpp>
#include <reloco/checked.hpp>
#include <reloco/checked_value.hpp>
#include <reloco/collection_view.hpp>
#include <reloco/commit.hpp>
#include <reloco/concepts.hpp>
#include <reloco/construction_helpers.hpp>
#include <reloco/container_ref.hpp>
#include <reloco/container_ref_std.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/digraph.hpp>
#include <reloco/duration.hpp>
#include <reloco/epoch.hpp>
#include <reloco/error.hpp>
#include <reloco/error_std.hpp>
#include <reloco/expected.hpp>
#include <reloco/fault_injection.hpp>
#include <reloco/fault_injection_patterns.hpp>
#include <reloco/flat_hash_map.hpp>
#include <reloco/flat_hash_set.hpp>
#include <reloco/flat_map.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/function.hpp>
#include <reloco/function_ref.hpp>
#include <reloco/futex.hpp>
#include <reloco/guarded_mutex.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/hint.hpp>
#include <reloco/inline_flat_map.hpp>
#include <reloco/inline_flat_set.hpp>
#include <reloco/inline_string.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/inplace_function.hpp>
#include <reloco/instant.hpp>
#include <reloco/int_ops.hpp>
#include <reloco/intrusive_c_list.hpp>
#include <reloco/intrusive_c_slist.hpp>
#include <reloco/intrusive_c_stailq.hpp>
#include <reloco/intrusive_hash_table.hpp>
#include <reloco/intrusive_iteration.hpp>
#include <reloco/iterator.hpp>
#include <reloco/keyed_intrusive_registry.hpp>
#include <reloco/lazy_lock.hpp>
#include <reloco/lifetime.hpp>
#include <reloco/lru_cache.hpp>
#include <reloco/masked_byte_region.hpp>
#include <reloco/masked_pointer.hpp>
#include <reloco/mutex.hpp>
#include <reloco/once.hpp>
#include <reloco/once_lock.hpp>
#include <reloco/optional.hpp>
#include <reloco/park.hpp>
#include <reloco/pool_allocator.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/rvalue_safety.hpp>
#include <reloco/rw_lock.hpp>
#include <reloco/scope.hpp>
#include <reloco/send_sync.hpp>
#include <reloco/seqlock.hpp>
#include <reloco/shared_ptr.hpp>
#include <reloco/span.hpp>
#include <reloco/spin_lock.hpp>
#include <reloco/stack_allocator.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>
#include <reloco/tamper.hpp>
#include <reloco/thread.hpp>
#include <reloco/tls_provider.hpp>
#include <reloco/tls_slot_vector.hpp>
#include <reloco/tree_map.hpp>
#include <reloco/tree_set.hpp>
#include <reloco/type_id.hpp>
#include <reloco/uninit.hpp>
#include <reloco/unique_ptr.hpp>
#include <reloco/value_ptr.hpp>
#include <reloco/value_ref.hpp>
#include <reloco/variant.hpp>
#include <reloco/vector.hpp>
#include <reloco/wait_group.hpp>

#include <reloco/detail/assert.hpp>
#include <reloco/detail/compat.hpp>
#include <reloco/detail/flat_container_base.hpp>
#include <reloco/detail/sanitizer.hpp>

#ifndef _MSC_VER
#include <reloco/reloco_ipc_ring.hpp>
#endif
