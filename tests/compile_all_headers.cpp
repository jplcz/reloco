// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Compiles every public reloco header under each supported C++ standard.
// Used by RELOCO_BUILD_HEADER_CHECKS and scripts/check-unsafe-buffer-usage.sh.

#include <reloco/allocator.hpp>
#include <reloco/array.hpp>
#include <reloco/checked_value.hpp>
#include <reloco/concepts.hpp>
#include <reloco/construction_helpers.hpp>
#include <reloco/default_allocator.hpp>
#include <reloco/error.hpp>
#include <reloco/expected.hpp>
#include <reloco/function.hpp>
#include <reloco/heap_allocator.hpp>
#include <reloco/lifetime.hpp>
#include <reloco/relocatable.hpp>
#include <reloco/rvalue_safety.hpp>
#include <reloco/shared_ptr.hpp>
#include <reloco/span.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>
#include <reloco/unique_ptr.hpp>
#include <reloco/value_ptr.hpp>
#include <reloco/value_ref.hpp>

#include <reloco/detail/assert.hpp>
#include <reloco/detail/compat.hpp>
