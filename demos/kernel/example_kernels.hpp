// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

// Four general-purpose kernels written with the bounds-proven kernel framework (C++20), each with a plain scalar twin
// in `kernel_examples::scalar` that the demo and the tests use as an oracle. Every kernel validates its arguments once,
// then runs loops with no per-element checks and no "Proof:" comments: the proof is the binding and the proven indexes.
// All arithmetic is integer; see docs/kernel/examples.md.

#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace kernel_examples {

namespace k = reloco::kernel;
using reloco::span;

// Clamps a 64-bit value into the range of the integer type T.
template <class T> [[nodiscard]] constexpr T clamp_to(std::int64_t v) noexcept {
  constexpr std::int64_t lo = std::numeric_limits<T>::min();
  constexpr std::int64_t hi = std::numeric_limits<T>::max();
  return static_cast<T>(v < lo ? lo : (v > hi ? hi : v));
}

// ---------------------------------------------------------------------------------------------------------------
// 1. rescale: out[i] = clamp(round(in[i] * multiplier / 2^shift) + offset). Fixed-point resampling of an integer
//    signal into a narrower type. Pattern: two equal-length vectors, one binding, one loop.
// ---------------------------------------------------------------------------------------------------------------
[[nodiscard]] inline reloco::result<void> rescale(span<std::int8_t> out, span<const std::int32_t> in,
                                                  std::int32_t multiplier, unsigned shift,
                                                  std::int32_t offset) noexcept {
  if (shift < 1 || shift > 62)
    return reloco::unexpected(reloco::error::invalid_argument);
  // The one-time check: `out` holds at least in.size() elements and does not overlap `in`.
  auto bound = RELOCO_KERNEL_BIND(in.size(), out, in);
  if (!bound)
    return reloco::unexpected(bound.error());
  auto dst = bound->view<0>();
  auto src = bound->view<1>();
  const std::int64_t half = std::int64_t{1} << (shift - 1);
  for (auto i : bound->domain()) { // i is a proven index: always < in.size()
    const std::int64_t scaled = (static_cast<std::int64_t>(src(i)) * multiplier + half) >> shift;
    dst(i) = clamp_to<std::int8_t>(scaled + offset);
  }
  return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 2. rotate_pairs: data holds (x, y) pairs; `cos_q15` and `sin_q15` hold one Q15 fixed-point value per pair, so they
//    have half as many entries as `data`. Each point is rotated: x' = x*cos - y*sin, y' = x*sin + y*cos.
//    Pattern: adjacent pairs through `pairs()`, plus per-block tables through `try_blockwise`.
// ---------------------------------------------------------------------------------------------------------------
[[nodiscard]] inline reloco::result<void> rotate_pairs(span<std::int16_t> data, span<const std::int16_t> cos_q15,
                                                       span<const std::int16_t> sin_q15) noexcept {
  if (data.size() % 2 != 0)
    return reloco::unexpected(reloco::error::invalid_argument);
  auto bound = RELOCO_KERNEL_BIND(data.size(), data);
  if (!bound)
    return reloco::unexpected(bound.error());
  // One-time check: each table has data.size() / 2 entries and does not overlap `data`.
  auto cosines = bound->try_blockwise<2>(cos_q15);
  auto sines = bound->try_blockwise<2>(sin_q15);
  if (!cosines)
    return reloco::unexpected(cosines.error());
  if (!sines)
    return reloco::unexpected(sines.error());
  auto points = bound->view<0>();
  auto cos_table = cosines.value();
  auto sin_table = sines.value();
  for (auto blk : bound->pairs()) { // each block covers elements 2p and 2p + 1; table entry p exists
    const auto xy = points.load(blk);
    const std::int64_t x = xy[0], y = xy[1], c = cos_table(blk), s = sin_table(blk);
    const std::int64_t rx = (x * c - y * s) >> 15;
    const std::int64_t ry = (x * s + y * c) >> 15;
    points.store(blk, {clamp_to<std::int16_t>(rx), clamp_to<std::int16_t>(ry)});
  }
  return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 3. row_dots: out[r] = dot(row r of m, x) for a matrix with arbitrary row stride (rows may be padded). Rows are
//    processed four at a time, then the leftover rows one at a time. Pattern: tensor_view + projection + blocks.
// ---------------------------------------------------------------------------------------------------------------
namespace detail {
// The elements of row `r`, from its first element on. The caller's loop over row blocks guarantees `r < rows`.
[[nodiscard]] inline reloco::result<span<const std::int8_t>> row_span(const k::tensor_view<const std::int8_t, 2> &m,
                                                                      std::size_t r) noexcept {
  auto row = k::try_row(m, r);
  if (!row)
    return reloco::unexpected(row.error());
  return row.value().storage();
}
} // namespace detail

[[nodiscard]] inline reloco::result<void> row_dots(const k::tensor_view<const std::int8_t, 2> &m,
                                                   span<const std::int8_t> x, span<std::int64_t> out) noexcept {
  const std::size_t rows = m.extent(0);
  const std::size_t len = m.extent(1);
  if (len > 1 && m.stride(1) != 1) // the inner loop walks each row as consecutive elements
    return reloco::unexpected(reloco::error::invalid_argument);
  if (out.size() < rows || x.size() < len)
    return reloco::unexpected(reloco::error::out_of_bounds);

  for (auto blk : k::blocks<4>(rows)) {
    const std::size_t r = blk.base();
    auto s0 = detail::row_span(m, r);
    auto s1 = detail::row_span(m, r + 1);
    auto s2 = detail::row_span(m, r + 2);
    auto s3 = detail::row_span(m, r + 3);
    if (!s0 || !s1 || !s2 || !s3)
      return reloco::unexpected(reloco::error::out_of_bounds);
    // All five spans are read-only, so they may overlap; each must hold at least `len` elements.
    auto bound = RELOCO_KERNEL_BIND(len, s0.value(), s1.value(), s2.value(), s3.value(), x);
    if (!bound)
      return reloco::unexpected(bound.error());
    auto r0 = bound->view<0>();
    auto r1 = bound->view<1>();
    auto r2 = bound->view<2>();
    auto r3 = bound->view<3>();
    auto xs = bound->view<4>();
    std::int64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    for (auto i : bound->domain()) { // one load of x[i] feeds four rows
      const std::int64_t xv = xs(i);
      a0 += xv * r0(i);
      a1 += xv * r1(i);
      a2 += xv * r2(i);
      a3 += xv * r3(i);
    }
    out[r] = a0;
    out[r + 1] = a1;
    out[r + 2] = a2;
    out[r + 3] = a3;
  }
  for (auto row_index : k::tail<4>(rows)) {
    auto s = detail::row_span(m, row_index.value());
    if (!s)
      return reloco::unexpected(s.error());
    auto bound = RELOCO_KERNEL_BIND(len, s.value(), x);
    if (!bound)
      return reloco::unexpected(bound.error());
    auto rv = bound->view<0>();
    auto xs = bound->view<1>();
    std::int64_t acc = 0;
    for (auto i : bound->domain())
      acc += static_cast<std::int64_t>(xs(i)) * rv(i);
    out[row_index.value()] = acc;
  }
  return {};
}

// ---------------------------------------------------------------------------------------------------------------
// 4. moving_sum: out[s] = in[s] + ... + in[s + w - 1] for every window start s. `out` needs in.size() elements; only
//    the first in.size() - w + 1 are written. Pattern: sliding windows through `windows(w)`.
// ---------------------------------------------------------------------------------------------------------------
[[nodiscard]] inline reloco::result<void> moving_sum(span<std::int64_t> out, span<const std::int32_t> in,
                                                     std::size_t w) noexcept {
  if (w == 0 || w > in.size())
    return reloco::unexpected(reloco::error::invalid_argument);
  auto bound = RELOCO_KERNEL_BIND(in.size(), out, in);
  if (!bound)
    return reloco::unexpected(bound.error());
  auto dst = bound->view<0>();
  auto src = bound->view<1>();
  for (auto win : bound->windows(w)) { // every window lies inside [0, in.size())
    std::int64_t sum = 0;
    for (std::int32_t v : src.subspan(win))
      sum += v;
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    // The window is non-empty (w >= 1) and its start is below in.size(), the bound extent of `dst`.
    auto slot = dst.subspan(win);
    slot.unchecked_at(0) = sum;
    RELOCO_END_UNSAFE_BUFFER_USAGE
  }
  return {};
}

// ---------------------------------------------------------------------------------------------------------------
// Scalar twins: plain, fully checked loops used as the oracle. Same signatures, same results.
// ---------------------------------------------------------------------------------------------------------------
namespace scalar {

[[nodiscard]] inline reloco::result<void> rescale(span<std::int8_t> out, span<const std::int32_t> in,
                                                  std::int32_t multiplier, unsigned shift,
                                                  std::int32_t offset) noexcept {
  if (shift < 1 || shift > 62)
    return reloco::unexpected(reloco::error::invalid_argument);
  return k::reference::try_transform(out, in, [&](std::int32_t v) {
    const std::int64_t scaled = (static_cast<std::int64_t>(v) * multiplier + (std::int64_t{1} << (shift - 1))) >> shift;
    return clamp_to<std::int8_t>(scaled + offset);
  });
}

[[nodiscard]] inline reloco::result<void> rotate_pairs(span<std::int16_t> data, span<const std::int16_t> cos_q15,
                                                       span<const std::int16_t> sin_q15) noexcept {
  if (data.size() % 2 != 0)
    return reloco::unexpected(reloco::error::invalid_argument);
  if (cos_q15.size() < data.size() / 2 || sin_q15.size() < data.size() / 2)
    return reloco::unexpected(reloco::error::out_of_bounds);
  for (std::size_t p = 0; p < data.size() / 2; ++p) {
    const std::int64_t x = data[2 * p], y = data[2 * p + 1], c = cos_q15[p], s = sin_q15[p];
    data[2 * p] = clamp_to<std::int16_t>((x * c - y * s) >> 15);
    data[2 * p + 1] = clamp_to<std::int16_t>((x * s + y * c) >> 15);
  }
  return {};
}

[[nodiscard]] inline reloco::result<void> row_dots(const k::tensor_view<const std::int8_t, 2> &m,
                                                   span<const std::int8_t> x, span<std::int64_t> out) noexcept {
  if (m.extent(1) > 1 && m.stride(1) != 1)
    return reloco::unexpected(reloco::error::invalid_argument);
  if (out.size() < m.extent(0) || x.size() < m.extent(1))
    return reloco::unexpected(reloco::error::out_of_bounds);
  for (std::size_t r = 0; r < m.extent(0); ++r) {
    std::int64_t acc = 0;
    for (std::size_t c = 0; c < m.extent(1); ++c)
      acc += static_cast<std::int64_t>(m.at(r, c)) * x[c];
    out[r] = acc;
  }
  return {};
}

[[nodiscard]] inline reloco::result<void> moving_sum(span<std::int64_t> out, span<const std::int32_t> in,
                                                     std::size_t w) noexcept {
  if (w == 0 || w > in.size())
    return reloco::unexpected(reloco::error::invalid_argument);
  if (out.size() < in.size())
    return reloco::unexpected(reloco::error::out_of_bounds);
  for (std::size_t s = 0; s + w <= in.size(); ++s) {
    std::int64_t sum = 0;
    for (std::size_t j = 0; j < w; ++j)
      sum += in[s + j];
    out[s] = sum;
  }
  return {};
}

} // namespace scalar

} // namespace kernel_examples
