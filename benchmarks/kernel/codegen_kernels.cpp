// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// The same kernel written four ways. scripts/check-kernel-codegen.sh compiles this file to assembly and checks that the
// bound version's hot loop matches the hand-written unchecked one, while the checked version keeps its bounds checks.
// Every function is extern "C" with a fixed name so the script can find it.

#include <reloco/kernel/kernel.hpp>
#include <reloco/span.hpp>

#include <cstddef>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace k = reloco::kernel;

extern "C" {

// Plain pointers: the reference for what the loop should cost.
void scale_raw(int *out, const int *in, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i)
    out[i] = in[i] * 3;
}

// span::operator[] checks every access.
void scale_checked(reloco::span<int> out, reloco::span<const int> in) {
  for (std::size_t i = 0; i < in.size(); ++i)
    out[i] = in[i] * 3;
}

// Hand-proven: one check outside the loop, unchecked_at inside.
void scale_unchecked(reloco::span<int> out, reloco::span<const int> in) {
  if (out.size() < in.size())
    return;
  for (std::size_t i = 0; i < in.size(); ++i)
    out.unchecked_at(i) = in.unchecked_at(i) * 3;
}

// The kernel framework: bind once, loop over proven indexes.
void scale_bound(reloco::span<int> out, reloco::span<const int> in) {
  auto b = k::try_bind<struct scale_brand>(in.size(), out, in);
  if (!b)
    return;
  auto dst = b.value().view<0>();
  auto src = b.value().view<1>();
  for (auto i : b.value().domain())
    dst(i) = src(i) * 3;
}

// The library algorithm.
void scale_algo(reloco::span<int> out, reloco::span<const int> in) {
  (void)k::try_transform(out, in, [](int x) { return x * 3; });
}
}

RELOCO_END_UNSAFE_BUFFER_USAGE
