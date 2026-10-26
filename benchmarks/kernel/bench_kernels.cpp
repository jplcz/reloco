// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Times the same kernel written five ways (see codegen_kernels.cpp). Prints integer nanoseconds per pass, best of
// several repetitions, so it needs no floating point. Run it on a quiet machine, built with optimization.

#include <reloco/span.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" {
void scale_raw(int *out, const int *in, std::size_t n);
void scale_checked(reloco::span<int> out, reloco::span<const int> in);
void scale_unchecked(reloco::span<int> out, reloco::span<const int> in);
void scale_bound(reloco::span<int> out, reloco::span<const int> in);
void scale_algo(reloco::span<int> out, reloco::span<const int> in);
}

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

namespace {

constexpr int repetitions = 15;

template <class Fn> std::int64_t best_ns(std::size_t passes, Fn &&fn) {
  std::int64_t best = INT64_MAX;
  for (int r = 0; r < repetitions; ++r) {
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t p = 0; p < passes; ++p) {
      fn();
      asm volatile("" ::: "memory"); // keep the compiler from merging or dropping passes
    }
    const auto stop = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    if (ns < best)
      best = ns;
  }
  return best / static_cast<std::int64_t>(passes);
}

void run(std::size_t n, std::size_t passes) {
  std::vector<int> in(n, 2);
  std::vector<int> out(n, 0);
  reloco::span<int> o(out);
  reloco::span<const int> i(in);
  std::printf("n = %zu (ns per pass, best of %d)\n", n, repetitions);
  std::printf("  raw pointers : %lld\n",
              static_cast<long long>(best_ns(passes, [&] { scale_raw(out.data(), in.data(), n); })));
  std::printf("  checked span : %lld\n", static_cast<long long>(best_ns(passes, [&] { scale_checked(o, i); })));
  std::printf("  unchecked_at : %lld\n", static_cast<long long>(best_ns(passes, [&] { scale_unchecked(o, i); })));
  std::printf("  bound domain : %lld\n", static_cast<long long>(best_ns(passes, [&] { scale_bound(o, i); })));
  std::printf("  try_transform: %lld\n", static_cast<long long>(best_ns(passes, [&] { scale_algo(o, i); })));
}

} // namespace

int main() {
  run(16, 200000); // the one-time binding cost is visible here
  run(1024, 20000);
  run(1 << 20, 50); // memory bound; the variants should converge
  return 0;
}

RELOCO_END_UNSAFE_BUFFER_USAGE
