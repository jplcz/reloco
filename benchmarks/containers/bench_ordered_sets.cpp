// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Compares the three ordered set containers on the same workloads:
//   tree_set : unbalanced node tree  (fast on random data, O(n) per op on sorted data)
//   set      : balanced red-black node tree (O(log n) worst case)
//   flat_set : sorted vector (fastest lookup/iteration, O(n) insert/erase)
// The same workloads then run on tree_map/map/flat_map with reloco::string keys and values.
// Prints integer nanoseconds per operation (best of several repetitions), so it needs no floating point. Run on a
// quiet machine, built with optimization (see benchmarks/README.md).

#include <reloco/flat_map.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/map.hpp>
#include <reloco/set.hpp>
#include <reloco/string.hpp>
#include <reloco/string_view.hpp>
#include <reloco/tree_map.hpp>
#include <reloco/tree_set.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

constexpr int repetitions = 5;

// Above this size the unbalanced tree is skipped on sorted input: it degrades to O(n^2) total work there.
constexpr std::size_t tree_set_sorted_limit = 20000;

struct xorshift {
  std::uint64_t state = 0x9E3779B97F4A7C15ULL;
  std::uint64_t next() noexcept {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
};

std::vector<std::uint32_t> sorted_keys(std::size_t n) {
  std::vector<std::uint32_t> keys;
  keys.reserve(n);
  for (std::size_t i = 0; i < n; ++i)
    keys.push_back(static_cast<std::uint32_t>(i));
  return keys;
}

// Distinct keys in random order: a Fisher-Yates shuffle of 0..n-1.
std::vector<std::uint32_t> shuffled_keys(std::size_t n) {
  auto keys = sorted_keys(n);
  xorshift rng;
  for (std::size_t i = n; i > 1; --i) {
    const auto j = static_cast<std::size_t>(rng.next() % i);
    const auto tmp = keys[i - 1];
    keys[i - 1] = keys[j];
    keys[j] = tmp;
  }
  return keys;
}

template <class Fn> std::int64_t best_ns(Fn &&fn) {
  std::int64_t best = INT64_MAX;
  for (int r = 0; r < repetitions; ++r) {
    const auto start = std::chrono::steady_clock::now();
    fn();
    asm volatile("" ::: "memory");
    const auto stop = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    if (ns < best)
      best = ns;
  }
  return best;
}

template <class Set> Set make_set() {
  auto res = Set::try_create();
  if (!res)
    std::abort();
  return std::move(*res);
}

template <class Set> void fill(Set &set, const std::vector<std::uint32_t> &keys) {
  for (const auto key : keys) {
    if (!set.try_insert(key))
      std::abort();
  }
}

template <class Set> std::int64_t bench_insert(const std::vector<std::uint32_t> &keys) {
  return best_ns([&] {
    auto set = make_set<Set>();
    fill(set, keys);
  });
}

template <class Set>
std::int64_t bench_lookup(const std::vector<std::uint32_t> &keys, const std::vector<std::uint32_t> &probes) {
  auto set = make_set<Set>();
  fill(set, keys);
  std::size_t hits = 0;
  const auto ns = best_ns([&] {
    for (const auto key : probes)
      hits += set.contains(key) ? 1 : 0;
  });
  if (hits == 0)
    std::abort();
  return ns;
}

// Includes rebuilding the set each repetition; the erase cost is that total minus the matching insert time.
template <class Set>
std::int64_t bench_insert_erase(const std::vector<std::uint32_t> &keys, const std::vector<std::uint32_t> &erase_order) {
  return best_ns([&] {
    auto set = make_set<Set>();
    fill(set, keys);
    for (const auto key : erase_order) {
      if (!set.try_remove(key))
        std::abort();
    }
  });
}

// Steady-state churn: a set of n elements where each step erases the oldest key and inserts a fresh one (the set
// build itself is not timed). Reported per erase+insert pair.
template <class Set> std::int64_t bench_churn(const std::vector<std::uint32_t> &keys, std::size_t n) {
  std::int64_t best = INT64_MAX;
  for (int r = 0; r < repetitions; ++r) {
    auto set = make_set<Set>();
    for (std::size_t i = 0; i < n; ++i) {
      if (!set.try_insert(keys[i]))
        std::abort();
    }
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < n; ++i) {
      if (!set.try_remove(keys[i]) || !set.try_insert(keys[n + i]))
        std::abort();
    }
    const auto stop = std::chrono::steady_clock::now();
    asm volatile("" ::: "memory");
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    if (ns < best)
      best = ns;
  }
  return best;
}

// Drains the set smallest-first (priority-queue style) through try_pop_first.
template <class Set> std::int64_t bench_drain(const std::vector<std::uint32_t> &keys) {
  std::int64_t best = INT64_MAX;
  for (int r = 0; r < repetitions; ++r) {
    auto set = make_set<Set>();
    fill(set, keys);
    const auto start = std::chrono::steady_clock::now();
    while (!set.empty()) {
      if (!set.try_pop_first())
        std::abort();
    }
    const auto stop = std::chrono::steady_clock::now();
    asm volatile("" ::: "memory");
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    if (ns < best)
      best = ns;
  }
  return best;
}

template <class Set> std::int64_t bench_iterate(const std::vector<std::uint32_t> &keys) {
  auto set = make_set<Set>();
  fill(set, keys);
  std::uint64_t sum = 0;
  const auto ns = best_ns([&] {
    for (const auto key : set)
      sum += key;
  });
  if (sum == 0)
    std::abort();
  return ns;
}

void print_row(const char *name, std::size_t n, std::int64_t tree, std::int64_t balanced, std::int64_t flat) {
  const auto per_op = [n](std::int64_t total) {
    return total < 0 ? -1LL : static_cast<long long>(total) / static_cast<long long>(n);
  };
  std::printf("  %-24s", name);
  for (const auto ns : {tree, balanced, flat}) {
    if (ns < 0)
      std::printf(" %10s", "skipped");
    else
      std::printf(" %10lld", per_op(ns));
  }
  std::printf("\n");
}

void run(std::size_t n) {
  const auto sorted = sorted_keys(n);
  const auto random = shuffled_keys(n);
  const bool tree_ok_sorted = n <= tree_set_sorted_limit;

  std::printf("n = %zu (ns per element, best of %d)\n", n, repetitions);
  std::printf("  %-24s %10s %10s %10s\n", "", "tree_set", "set", "flat_set");

  print_row("insert sorted", n, tree_ok_sorted ? bench_insert<reloco::tree_set<std::uint32_t>>(sorted) : -1,
            bench_insert<reloco::set<std::uint32_t>>(sorted), bench_insert<reloco::flat_set<std::uint32_t>>(sorted));
  print_row("insert random", n, bench_insert<reloco::tree_set<std::uint32_t>>(random),
            bench_insert<reloco::set<std::uint32_t>>(random), bench_insert<reloco::flat_set<std::uint32_t>>(random));
  print_row("lookup (random tree)", n, bench_lookup<reloco::tree_set<std::uint32_t>>(random, random),
            bench_lookup<reloco::set<std::uint32_t>>(random, random),
            bench_lookup<reloco::flat_set<std::uint32_t>>(random, random));
  print_row("lookup (sorted-built)", n,
            tree_ok_sorted ? bench_lookup<reloco::tree_set<std::uint32_t>>(sorted, random) : -1,
            bench_lookup<reloco::set<std::uint32_t>>(sorted, random),
            bench_lookup<reloco::flat_set<std::uint32_t>>(sorted, random));
  print_row("insert+erase random", n, bench_insert_erase<reloco::tree_set<std::uint32_t>>(random, random),
            bench_insert_erase<reloco::set<std::uint32_t>>(random, random),
            bench_insert_erase<reloco::flat_set<std::uint32_t>>(random, random));
  const auto churn_keys = shuffled_keys(2 * n);
  print_row("churn (erase+insert)", n, bench_churn<reloco::tree_set<std::uint32_t>>(churn_keys, n),
            bench_churn<reloco::set<std::uint32_t>>(churn_keys, n),
            bench_churn<reloco::flat_set<std::uint32_t>>(churn_keys, n));
  // flat_set's pop-from-front is O(n) per pop, so it is not part of this comparison.
  print_row("drain (pop_first)", n, bench_drain<reloco::tree_set<std::uint32_t>>(random),
            bench_drain<reloco::set<std::uint32_t>>(random), -1);
  print_row("iterate (random tree)", n, bench_iterate<reloco::tree_set<std::uint32_t>>(random),
            bench_iterate<reloco::set<std::uint32_t>>(random), bench_iterate<reloco::flat_set<std::uint32_t>>(random));
  std::printf("\n");
}

// ---- string/string maps -----------------------------------------------------------------------------------------
// Keys are 16-digit zero-padded decimals (so string order equals numeric order) and values are 24 characters. Strings
// are move-only, so every insert clones a prebuilt key/value inside the timed region; that cost is identical for all
// three containers and is part of the reported ns/element.

struct str_entry {
  reloco::string key;
  reloco::string value;
};

reloco::string make_string(const char *text, std::size_t len) {
  auto res = reloco::string::try_create(reloco::string_view(text, len));
  if (!res)
    std::abort();
  return std::move(*res);
}

reloco::string clone_string(const reloco::string &str) {
  auto res = str.try_clone();
  if (!res)
    std::abort();
  return std::move(*res);
}

std::vector<str_entry> make_entries(const std::vector<std::uint32_t> &order) {
  std::vector<str_entry> entries;
  entries.reserve(order.size());
  for (const auto id : order) {
    char key[17];
    char value[25];
    std::snprintf(key, sizeof key, "%016u", id);
    std::snprintf(value, sizeof value, "value-%018u", id);
    entries.push_back(str_entry{make_string(key, 16), make_string(value, 24)});
  }
  return entries;
}

template <class Map> Map make_map() {
  auto res = Map::try_create();
  if (!res)
    std::abort();
  return std::move(*res);
}

template <class Map> void fill_map(Map &map, const std::vector<str_entry> &entries) {
  for (const auto &entry : entries) {
    if (!map.try_insert(clone_string(entry.key), clone_string(entry.value)))
      std::abort();
  }
}

template <class Map> std::int64_t bench_map_insert(const std::vector<str_entry> &entries) {
  return best_ns([&] {
    auto map = make_map<Map>();
    fill_map(map, entries);
  });
}

template <class Map>
std::int64_t bench_map_lookup(const std::vector<str_entry> &entries, const std::vector<str_entry> &probes) {
  auto map = make_map<Map>();
  fill_map(map, entries);
  std::size_t hits = 0;
  const auto ns = best_ns([&] {
    for (const auto &probe : probes)
      hits += map.contains(probe.key) ? 1 : 0;
  });
  if (hits == 0)
    std::abort();
  return ns;
}

template <class Map>
std::int64_t bench_map_insert_erase(const std::vector<str_entry> &entries, const std::vector<str_entry> &erase_order) {
  return best_ns([&] {
    auto map = make_map<Map>();
    fill_map(map, entries);
    for (const auto &entry : erase_order) {
      if (!map.try_remove(entry.key))
        std::abort();
    }
  });
}

template <class Map> std::int64_t bench_map_iterate(const std::vector<str_entry> &entries) {
  auto map = make_map<Map>();
  fill_map(map, entries);
  std::size_t total = 0;
  const auto ns = best_ns([&] {
    for (const auto &kv : map)
      total += kv.second.size();
  });
  if (total == 0)
    std::abort();
  return ns;
}

void run_maps(std::size_t n) {
  using tree_m = reloco::tree_map<reloco::string, reloco::string>;
  using bal_m = reloco::map<reloco::string, reloco::string>;
  using flat_m = reloco::flat_map<reloco::string, reloco::string>;

  const auto sorted = make_entries(sorted_keys(n));
  const auto random = make_entries(shuffled_keys(n));
  const bool tree_ok_sorted = n <= tree_set_sorted_limit;

  std::printf("string/string maps, n = %zu (ns per element, best of %d)\n", n, repetitions);
  std::printf("  %-24s %10s %10s %10s\n", "", "tree_map", "map", "flat_map");

  print_row("insert sorted", n, tree_ok_sorted ? bench_map_insert<tree_m>(sorted) : -1, bench_map_insert<bal_m>(sorted),
            bench_map_insert<flat_m>(sorted));
  print_row("insert random", n, bench_map_insert<tree_m>(random), bench_map_insert<bal_m>(random),
            bench_map_insert<flat_m>(random));
  print_row("lookup (random tree)", n, bench_map_lookup<tree_m>(random, random),
            bench_map_lookup<bal_m>(random, random), bench_map_lookup<flat_m>(random, random));
  print_row("lookup (sorted-built)", n, tree_ok_sorted ? bench_map_lookup<tree_m>(sorted, random) : -1,
            bench_map_lookup<bal_m>(sorted, random), bench_map_lookup<flat_m>(sorted, random));
  print_row("insert+erase random", n, bench_map_insert_erase<tree_m>(random, random),
            bench_map_insert_erase<bal_m>(random, random), bench_map_insert_erase<flat_m>(random, random));
  print_row("iterate (random tree)", n, bench_map_iterate<tree_m>(random), bench_map_iterate<bal_m>(random),
            bench_map_iterate<flat_m>(random));
  std::printf("\n");
}

} // namespace

int main() {
  for (const std::size_t n : {1000u, 10000u, 100000u})
    run(n);
  for (const std::size_t n : {1000u, 10000u, 100000u})
    run_maps(n);
  return 0;
}
