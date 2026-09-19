#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly build_dir="${1:-${source_dir}/build-sanitizers}"
readonly compiler="${CXX:-clang++}"
readonly parallel="${JPLCZ_RELOCO_BUILD_PARALLEL:-2}"

if (($# >= 1)); then
  shift
fi

cmake \
  -S "${source_dir}" \
  -B "${build_dir}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  "-DCMAKE_CXX_COMPILER=${compiler}" \
  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer" \
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined" \
  -DJPLCZ_RELOCO_BUILD_HEADER_CHECKS=OFF \
  "$@"

cmake --build "${build_dir}" --parallel "${parallel}"

ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:strict_string_checks=1}" \
UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}" \
  ctest --test-dir "${build_dir}" --output-on-failure
