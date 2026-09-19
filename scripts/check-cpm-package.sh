#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly build_dir="${JPLCZ_RELOCO_CPM_BUILD_DIR:-${source_dir}/build/cpm}"
readonly cpm_version="${JPLCZ_RELOCO_CPM_VERSION:-0.43.1}"
readonly cpm_path="${JPLCZ_RELOCO_CPM_PATH:-${build_dir}/CPM.cmake}"
readonly parallel="${JPLCZ_RELOCO_BUILD_PARALLEL:-2}"

if [[ ! -f "${cpm_path}" ]]; then
  if ! command -v curl >/dev/null 2>&1; then
    printf 'error: curl is required to download CPM.cmake\n' >&2
    exit 1
  fi
  mkdir -p "$(dirname -- "${cpm_path}")"
  curl --fail --location --silent --show-error \
    "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${cpm_version}/CPM.cmake" \
    --output "${cpm_path}"
fi

cmake \
  -S "${source_dir}/tests/cmake/cpm" \
  -B "${build_dir}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  "-DCPM_CMAKE_PATH=${cpm_path}" \
  "-DJPLCZ_RELOCO_SOURCE_DIR=${source_dir}" \
  "$@"

cmake --build "${build_dir}" --parallel "${parallel}"
"${build_dir}/cpm_consumer"
