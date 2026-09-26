#!/bin/sh
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# Vagrant shell provisioner for the "openbsd" machine. Installs only the
# build tooling needed to run scripts/run-native-ci.sh; the C++ compiler
# is OpenBSD's own base "cc"/"c++" (Clang), which is never touched here.

set -eu

export PKG_PATH="https://cdn.openbsd.org/pub/OpenBSD/$(uname -r)/packages/$(machine -a)/"

pkg_add -Iz cmake ninja bash rsync git

echo "== base compiler =="
cc --version
c++ --version
