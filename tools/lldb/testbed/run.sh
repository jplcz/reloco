#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
#
# Builds the shared GDB testbed (tools/gdb/testbed/testbed.cpp) with Clang and
# checks every `// GDB_CHECK:` expectation against the output of LLDB's
# `frame variable` with tools/lldb/reloco_printers.py imported. The LLDB
# summaries deliberately match the GDB ones, so one set of markers serves both.
#
# Usage: tools/lldb/testbed/run.sh [--keep-build]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
TESTBED_SRC="${REPO_ROOT}/tools/gdb/testbed/testbed.cpp"
BUILD_DIR="${SCRIPT_DIR}/build"
CXX="${CXX:-clang++-24}"
LLDB="${LLDB:-lldb-24}"

KEEP_BUILD=0
if [[ "${1:-}" == "--keep-build" ]]; then
  KEEP_BUILD=1
fi

cleanup() {
  if [[ "${KEEP_BUILD}" -eq 0 ]]; then
    rm -rf "${BUILD_DIR}"
  fi
}
trap cleanup EXIT

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

echo "==> Compiling testbed with ${CXX}"
"${CXX}" -std=c++17 -g -O0 -I "${REPO_ROOT}/include" "${TESTBED_SRC}" -o "${BUILD_DIR}/testbed"

BREAK_LINE="$(grep -n '// GDB_BREAK$' "${TESTBED_SRC}" | head -1 | cut -d: -f1)"
if [[ -z "${BREAK_LINE}" ]]; then
  echo "error: could not find '// GDB_BREAK' marker in testbed.cpp" >&2
  exit 1
fi

mapfile -t CHECKS < <(grep -oP '// GDB_CHECK:\s*\K.*=>.*' "${TESTBED_SRC}")
if [[ "${#CHECKS[@]}" -eq 0 ]]; then
  echo "error: found no '// GDB_CHECK:' comments in testbed.cpp" >&2
  exit 1
fi
echo "==> Found ${#CHECKS[@]} check(s), breakpoint at testbed.cpp:${BREAK_LINE}"

CMDFILE="${BUILD_DIR}/cmds.lldb"
{
  echo "command script import ${REPO_ROOT}/tools/lldb/reloco_printers.py"
  echo "breakpoint set --file testbed.cpp --line ${BREAK_LINE}"
  echo "run"
  for check in "${CHECKS[@]}"; do
    name="${check%%=>*}"
    name="${name#"${name%%[![:space:]]*}"}"
    name="${name%"${name##*[![:space:]]}"}"
    echo "frame variable ${name}"
  done
  echo "quit"
} > "${CMDFILE}"

transcript="$("${LLDB}" --batch --no-lldbinit -s "${CMDFILE}" "${BUILD_DIR}/testbed" 2>&1 || true)"
echo "${transcript}" > "${BUILD_DIR}/transcript.txt"

ok=1
for check in "${CHECKS[@]}"; do
  name="${check%%=>*}"
  expected="${check#*=>}"
  name="${name#"${name%%[![:space:]]*}"}"
  name="${name%"${name##*[![:space:]]}"}"
  expected="${expected#"${expected%%[![:space:]]*}"}"
  expected="${expected%"${expected##*[![:space:]]}"}"
  if grep -qF -- "${expected}" <<< "${transcript}"; then
    echo "  PASS  ${name}: found \"${expected}\""
  else
    echo "  FAIL  ${name}: expected substring \"${expected}\" not found"
    ok=0
  fi
done

if [[ "${ok}" -eq 1 ]]; then
  echo "==> All LLDB printer checks passed."
else
  echo "==> Some LLDB printer checks FAILED; transcript in ${BUILD_DIR} (use --keep-build)." >&2
  exit 1
fi
