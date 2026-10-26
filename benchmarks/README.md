# benchmarks

Timing programs, built only with `-DJPLCZ_RELOCO_BUILD_BENCHMARKS=ON`.

- `kernel/codegen_kernels.cpp`: one kernel written five ways (raw pointers, checked span, `unchecked_at`, bound
  domain, `try_transform`). Also the input of `scripts/check-kernel-codegen.sh`.
- `kernel/bench_kernels.cpp`: times those variants (integer nanoseconds, no floating point).

See `docs/kernel/benchmarks.md`.
