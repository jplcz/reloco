#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly build_dir="${JPLCZ_RELOCO_VCPKG_BUILD_DIR:-${source_dir}/build/vcpkg}"
readonly triplet="${JPLCZ_RELOCO_VCPKG_TRIPLET:-x64-linux}"
readonly parallel="${JPLCZ_RELOCO_BUILD_PARALLEL:-2}"

if [[ -z "${VCPKG_ROOT:-}" ]]; then
  printf 'error: VCPKG_ROOT must identify a bootstrapped vcpkg checkout\n' >&2
  exit 1
fi
if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then
  printf 'error: vcpkg executable not found: %s/vcpkg\n' "${VCPKG_ROOT}" >&2
  exit 1
fi

"${VCPKG_ROOT}/vcpkg" install jplcz-reloco \
  "--overlay-ports=${source_dir}/packaging/vcpkg/ports" \
  "--triplet=${triplet}"

cmake \
  -S "${source_dir}/tests/cmake/find_package" \
  -B "${build_dir}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
  "$@"

cmake --build "${build_dir}" --parallel "${parallel}"
"${build_dir}/find_package_consumer"
