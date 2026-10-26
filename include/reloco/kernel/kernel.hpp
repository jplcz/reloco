// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file kernel/kernel.hpp
 * @brief Umbrella header of the bounds-proven kernel framework (C++20 and newer).
 *
 * See `docs/kernel/README.md`.
 */

#include "algo/elementwise.hpp"
#include "algo/reduce.hpp"
#include "algo/reference.hpp"
#include "algo/scan.hpp"
#include "bind/bound.hpp"
#include "bind/bound_span.hpp"
#include "config.hpp"
#include "index/blocks.hpp"
#include "index/proven_index.hpp"
#include "index/range.hpp"
#include "index/strided.hpp"
#include "index/windows.hpp"
#include "project/permute.hpp"
#include "project/reshape.hpp"
#include "project/slice.hpp"
#include "project/transpose.hpp"
#include "view/lane_traits.hpp"
#include "view/shape.hpp"
#include "view/stride.hpp"
#include "view/tensor_view.hpp"
