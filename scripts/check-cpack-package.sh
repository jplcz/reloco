#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly build_dir="${JPLCZ_RELOCO_CPACK_BUILD_DIR:-${source_dir}/build/cpack}"
readonly parallel="${JPLCZ_RELOCO_BUILD_PARALLEL:-2}"

cmake \
  -S "${source_dir}" \
  -B "${build_dir}" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DJPLCZ_RELOCO_BUILD_TESTS=OFF \
  -DJPLCZ_RELOCO_BUILD_HEADER_CHECKS=OFF \
  -DJPLCZ_RELOCO_INSTALL=ON \
  "$@"

cmake --build "${build_dir}" --parallel "${parallel}"

(cd "${build_dir}" && cpack)

shopt -s nullglob
packages=("${build_dir}"/jplcz-reloco-*.tar.gz "${build_dir}"/jplcz-reloco-*.zip \
  "${build_dir}"/jplcz-reloco*.deb "${build_dir}"/jplcz-reloco-*.rpm)
shopt -u nullglob

if [[ ${#packages[@]} -eq 0 ]]; then
  printf 'error: cpack did not produce any package\n' >&2
  exit 1
fi

printf 'Generated packages:\n'
printf '  %s\n' "${packages[@]}"

archive="${build_dir}/jplcz-reloco-0.1.0-Linux.tar.gz"
if [[ ! -f "${archive}" ]]; then
  printf 'error: expected archive package %s was not generated\n' "${archive}" >&2
  exit 1
fi

extract_dir="${build_dir}/extracted"
rm -rf "${extract_dir}"
mkdir -p "${extract_dir}"
tar -xzf "${archive}" -C "${extract_dir}"

package_root="$(find "${extract_dir}" -mindepth 1 -maxdepth 1 -type d)"

for required in \
  "include/reloco/array.hpp" \
  "share/cmake/jplcz_reloco/jplcz_relocoConfig.cmake" \
  "share/cmake/jplcz_reloco/jplcz_relocoConfigVersion.cmake" \
  "share/cmake/jplcz_reloco/jplcz_relocoTargets.cmake" \
  "share/doc/jplcz_reloco/README.md" \
  "share/doc/jplcz_reloco/LICENSE" \
  "share/doc/jplcz_reloco/docs/hardened-containers.md"; do
  if [[ ! -f "${package_root}/${required}" ]]; then
    printf 'error: archive package is missing %s\n' "${required}" >&2
    exit 1
  fi
done

printf 'Archive package contents verified under %s\n' "${package_root}"
