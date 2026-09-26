#!/bin/sh
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# Vagrant shell provisioner for the "freebsd" machine. Installs only the
# build tooling needed to run scripts/run-native-ci.sh; the C++ compiler
# is FreeBSD's own base "cc"/"c++" (Clang), which is never touched here.

set -eu

env ASSUME_ALWAYS_YES=yes pkg bootstrap -f
pkg install -y cmake ninja bash rsync git

echo "== base compiler =="
cc --version
c++ --version
