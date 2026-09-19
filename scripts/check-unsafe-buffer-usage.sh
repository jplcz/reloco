#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

set -uo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly compiler="${RELOCO_UNSAFE_BUFFER_CXX:-clang++-24}"
readonly max_warnings="${RELOCO_UNSAFE_BUFFER_MAX_WARNINGS:-0}"
read -r -a standards <<<"${RELOCO_UNSAFE_BUFFER_STANDARDS:-17 20 23}"

if ! command -v "${compiler}" >/dev/null 2>&1; then
  printf 'error: Clang 24 compiler not found: %s\n' "${compiler}" >&2
  exit 1
fi

if ! "${compiler}" --version | head -n 1 | grep -Eq 'clang version 24(\.| )'; then
  printf 'error: %s is not Clang 24\n' "${compiler}" >&2
  "${compiler}" --version | head -n 1 >&2
  exit 1
fi

failures=()

for standard in "${standards[@]}"; do
  log_file="${TMPDIR:-/tmp}/reloco-unsafe-buffer-cxx${standard}.$$.log"

  printf '==> Checking C++%s with %s\n' "${standard}" "${compiler}"
  if ! "${compiler}" \
      "-std=c++${standard}" \
      "-DRELOCO_HEADER_CHECK_STANDARD=${standard}" \
      -I"${source_dir}/include" \
      -Wunsafe-buffer-usage \
      -fsyntax-only \
      "${source_dir}/tests/compile_all_headers.cpp" \
      >"${log_file}" 2>&1; then
    cat "${log_file}" >&2
    rm -f "${log_file}"
    failures+=("C++${standard}: compilation failed")
    continue
  fi

  warning_count="$(grep -c 'warning:' "${log_file}" || true)"
  printf '    unsafe-buffer diagnostics: %s (maximum: %s)\n' \
    "${warning_count}" "${max_warnings}"

  grep -E '^.*/include/reloco/[^:]+:[0-9]+:[0-9]+: warning:' \
      "${log_file}" |
    sed -E 's/:([0-9]+):([0-9]+): warning:.*//' |
    sort |
    uniq -c |
    sort -nr || true

  printf '    full diagnostic report:\n'
  cat "${log_file}"

  if ((warning_count > max_warnings)); then
    failures+=(
      "C++${standard}: ${warning_count} warnings exceed ${max_warnings}"
    )
  fi

  rm -f "${log_file}"
done

if ((${#failures[@]} != 0)); then
  printf '\nUnsafe-buffer analysis failed:\n' >&2
  printf '  - %s\n' "${failures[@]}" >&2
  exit 1
fi

printf '\nUnsafe-buffer diagnostic count is within the migration baseline.\n'
