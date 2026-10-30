# benchmarks

Timing programs, built only with `-DJPLCZ_RELOCO_BUILD_BENCHMARKS=ON`.

- `kernel/codegen_kernels.cpp`: one kernel written five ways (raw pointers, checked span, `unchecked_at`, bound
  domain, `try_transform`). Also the input of `scripts/check-kernel-codegen.sh`.
- `kernel/bench_kernels.cpp`: times those variants (integer nanoseconds, no floating point).
- `containers/bench_ordered_sets.cpp`: `tree_set` vs `set` vs `flat_set`, then `tree_map` vs `map` vs `flat_map` with
  `reloco::string` keys/values (insert sorted/random, lookup, insert+erase,
  iteration) at 1k/10k/100k elements; integer ns per element. Target `jplcz_reloco_container_bench`.

See `docs/kernel/benchmarks.md` for the kernel benchmarks; `docs/balanced-containers.md` for the container ones.
