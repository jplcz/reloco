#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Verifies the kernel framework's codegen contract (docs/kernel/benchmarks.md): compiles
# benchmarks/kernel/codegen_kernels.cpp to x86-64 assembly at -O2 with RELOCO_KERNEL_CHECKS=0 and checks that
#   - scale_bound, scale_algo and scale_unchecked contain no call, no trap and a vectorized loop;
#   - scale_checked still contains its bounds-check failure path (the control: proves the test can see checks).

set -uo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly compiler="${RELOCO_CODEGEN_CXX:-clang++-24}"

if ! command -v "${compiler}" >/dev/null 2>&1; then
  printf 'skip: compiler not found: %s\n' "${compiler}" >&2
  exit 77 # ctest SKIP_RETURN_CODE
fi

readonly asm_file="$(mktemp --suffix=.s)"
trap 'rm -f "${asm_file}"' EXIT

if ! "${compiler}" -std=c++20 -O2 -msse4.1 -target x86_64-linux-gnu -DRELOCO_KERNEL_CHECKS=0 \
  -Wno-unsafe-buffer-usage -I"${source_dir}/include" -S -o "${asm_file}" \
  "${source_dir}/benchmarks/kernel/codegen_kernels.cpp"; then
  printf 'error: compilation failed\n' >&2
  exit 1
fi

# Prints the body of one function (up to its .cfi_endproc).
function_body() {
  awk -v f="$1:" '$1 == f { p = 1 } p { print } p && /\.cfi_endproc/ { exit }' "${asm_file}"
}

status=0
for fn in scale_unchecked scale_bound scale_algo; do
  body="$(function_body "${fn}")"
  if [[ -z "${body}" ]]; then
    printf 'FAIL %s: function not found in assembly\n' "${fn}"
    status=1
    continue
  fi
  if grep -Eq '^\s(call|ud2)' <<<"${body}"; then
    printf 'FAIL %s: contains a call or trap in the hot path\n' "${fn}"
    status=1
  elif ! grep -Eq 'padd|pmul' <<<"${body}"; then
    printf 'FAIL %s: loop was not vectorized\n' "${fn}"
    status=1
  else
    printf 'ok   %s: no calls or traps, vectorized\n' "${fn}"
  fi
done

if grep -Eq '^\s(call|ud2)' <<<"$(function_body scale_checked)"; then
  printf 'ok   scale_checked: control still contains its bounds-check failure path\n'
else
  printf 'FAIL scale_checked: control has no check; the test cannot tell checks from no checks\n'
  status=1
fi
exit "${status}"
