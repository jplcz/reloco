# -*- mode: ruby -*-
# vi: set ft=ruby :

# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# Local, semi-automated BSD verification for reloco. See
# docs/bsd-vagrant-testing.md for prerequisites and usage; the short
# version is `./scripts/run-bsd-ci.sh freebsd` / `./scripts/run-bsd-ci.sh
# openbsd` (or `vagrant up <name> --provider=libvirt` directly).
#
# Only targets whose *native, out-of-the-box* base compiler already
# supports C++20 are included -- no compiler is ever built from source by
# these scripts. FreeBSD and OpenBSD both ship a reasonably current Clang
# as their base system compiler (`cc`/`c++`), so no extra toolchain
# package is installed at all; NetBSD's base GCC is too old for this
# project's C++20/C++23 header checks, and its pkgsrc binary package sets
# with a modern GCC/Clang only cover newer NetBSD releases than any
# well-maintained Vagrant box currently publishes, so it is intentionally
# left out for now (see the doc above for the full rationale).
Vagrant.configure("2") do |config|
  config.vm.provider :libvirt do |libvirt|
    libvirt.cpus = (ENV["RELOCO_BSD_VM_CPUS"] || "4").to_i
    libvirt.memory = (ENV["RELOCO_BSD_VM_MEMORY"] || "4096").to_i
  end

  # No VirtualBox Guest Additions / virtiofs on these guests, so fall
  # back to plain one-shot rsync for the synced folder -- works
  # identically under both the libvirt and virtualbox providers and only
  # requires `rsync` on the host (already required by Vagrant itself for
  # this folder type) and inside the guest (installed by the provisioning
  # scripts below).
  config.vm.synced_folder ".", "/vagrant",
    type: "rsync",
    rsync__exclude: [
      ".git/",
      ".vagrant/",
      "build/",
      "build-*/",
      "cmake-build-*/",
    ]

  config.vm.define "freebsd" do |freebsd|
    freebsd.vm.box = "generic/freebsd14"
    freebsd.vm.hostname = "reloco-freebsd"
    freebsd.vm.provision "shell", path: "scripts/vagrant/provision-freebsd.sh"
  end

  config.vm.define "openbsd" do |openbsd|
    openbsd.vm.box = "generic/openbsd7"
    openbsd.vm.hostname = "reloco-openbsd"
    openbsd.vm.provision "shell", path: "scripts/vagrant/provision-openbsd.sh"
  end
end
