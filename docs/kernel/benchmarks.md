# Does the proof really cost nothing? Codegen check and benchmark

## Why this exists

The kernel framework promises that after the one-time binding check, a hot loop runs as fast as a hand-written loop
with no bounds checks. A promise like that needs evidence, and evidence that survives future compiler and library
changes. Two tools provide it.

## 1. The codegen check

`scripts/check-kernel-codegen.sh` compiles `benchmarks/kernel/codegen_kernels.cpp` to assembly (`-O2`,
`RELOCO_KERNEL_CHECKS=0`) and inspects each variant of the same "multiply by 3" kernel:

| Variant | Written as | Expected |
| --- | --- | --- |
| `scale_unchecked` | one length test, then `unchecked_at` | no call, no trap, vectorized |
| `scale_bound` | `try_bind` + `domain()` + proven indexes | same |
| `scale_algo` | `try_transform` | same |
| `scale_checked` | `span::operator[]` | **control**: must still contain the failure path |

The control matters: if the checked version also showed no checks, the script could not tell "checks removed" from
"checks never visible", so it fails in that case. The script runs as ctest `KernelCodegen` and is skipped (exit 77)
when `clang++-24` is not available. Override the compiler with `RELOCO_CODEGEN_CXX`.

```sh
scripts/check-kernel-codegen.sh
```

## 2. The timing benchmark

```sh
cmake -DJPLCZ_RELOCO_BUILD_BENCHMARKS=ON -B build && cmake --build build --target jplcz_reloco_kernel_bench
./build/jplcz_reloco_kernel_bench
```

It prints integer nanoseconds per pass (best of 15) for n = 16, 1024 and 1048576. One run on a development machine
(your numbers will differ; compare variants against each other, not against this table):

| n | raw | checked span | `unchecked_at` | bound | `try_transform` |
| --- | --- | --- | --- | --- | --- |
| 16 | 1 | 4 | 2 | 2 | 3 |
| 1024 | 63 | 64 | 62 | 68 | 63 |
| 1048576 | 86710 | 85283 | 83860 | 85565 | 85812 |

How to read it: at large `n` the loop is memory bound and every variant converges. At tiny `n` the fixed cost of the
one-time binding (length and overlap checks) is a couple of nanoseconds; that is the price of making the loop provably
safe, paid once per call, not per element. Modern compilers can also hoist simple checks out of simple loops, so the
checked variant is often close here; the framework's value is that the guarantee holds for loops the optimizer cannot
untangle, and that the proof is in the types instead of in a comment.
