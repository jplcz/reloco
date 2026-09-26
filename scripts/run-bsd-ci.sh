#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# Boots one (or both) of the BSD Vagrant machines defined in the
# repository's Vagrantfile, rsyncs the working tree in, and runs
# scripts/run-native-ci.sh inside the guest using its native compiler.
#
# See docs/bsd-vagrant-testing.md for prerequisites.
#
# Usage:
#   scripts/run-bsd-ci.sh [--provider PROVIDER] [--destroy] [--] [MACHINE...]
#
# MACHINE is one or more of: freebsd openbsd all (default: all)

set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly all_machines=(freebsd openbsd)

provider="libvirt"
destroy_after=0
machines=()

while (($# > 0)); do
  case "$1" in
    --provider)
      provider="$2"
      shift 2
      ;;
    --destroy)
      destroy_after=1
      shift
      ;;
    --)
      shift
      machines+=("$@")
      break
      ;;
    *)
      machines+=("$1")
      shift
      ;;
  esac
done

if ((${#machines[@]} == 0)) || [[ "${machines[0]}" == "all" ]]; then
  machines=("${all_machines[@]}")
fi

cd "${source_dir}"

status=0
for machine in "${machines[@]}"; do
  echo "==> [${machine}] vagrant up --provider=${provider}"
  if ! vagrant up "${machine}" --provider="${provider}"; then
    echo "==> [${machine}] failed to boot/provision" >&2
    status=1
    continue
  fi

  echo "==> [${machine}] rsync working tree"
  vagrant rsync "${machine}"

  echo "==> [${machine}] running scripts/run-native-ci.sh"
  if ! vagrant ssh "${machine}" -c "cd /vagrant && bash scripts/run-native-ci.sh build-ci-${machine}"; then
    echo "==> [${machine}] build/test failed" >&2
    status=1
  fi

  if ((destroy_after)); then
    echo "==> [${machine}] vagrant destroy -f"
    vagrant destroy -f "${machine}"
  fi
done

exit "${status}"
