<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# BSD verification via Vagrant

## What it is for

reloco's day-to-day CI runs on Linux and Windows. `Vagrantfile` and
`scripts/run-bsd-ci.sh` add a **local, developer-run** way to build and
test the library on real BSD systems using disposable VMs, without
requiring BSD CI runners or a hand-maintained physical/virtual machine.

Only targets are included whose *native, out-of-the-box* system compiler
already supports C++20 well enough for this project:

| Machine   | Box                  | Base compiler                          |
| --------- | --------------------- | --------------------------------------- |
| `freebsd` | `generic/freebsd14`   | System Clang (`cc`/`c++`), no install |
| `openbsd` | `generic/openbsd7`    | System Clang (`cc`/`c++`), no install |

No compiler is ever built from source, and no extra compiler package is
installed either -- the provisioning scripts only add `cmake`, `ninja`,
`bash`, `rsync`, and `git` on top of each box's base install. NetBSD is
intentionally left out: its base compiler is an old GCC, and pkgsrc's
prebuilt binary package sets with a modern GCC/Clang only cover NetBSD
releases newer than any well-maintained Vagrant box currently publishes,
so using it here would mean building a compiler from source inside the
VM -- exactly what this setup avoids.

## Prerequisites

You need a Linux host with KVM available (`/dev/kvm` present, your user
allowed to use it) and the following installed:

- [Vagrant](https://developer.hashicorp.com/vagrant) (2.3+)
- `libvirt`/QEMU-KVM: `libvirtd` running, your user in the `libvirt`
  group, `virsh -c qemu:///system list` works without `sudo`
- Build headers for the `vagrant-libvirt` plugin: on Debian/Ubuntu,
  `sudo apt-get install -y libvirt-dev ruby-dev build-essential
  pkg-config`
- The `vagrant-libvirt` plugin itself:

  ```sh
  vagrant plugin install vagrant-libvirt
  ```

- `rsync` on the host (used for the synced folder; these BSD boxes have
  no Guest Additions/virtiofs support)

Verify the plugin is installed:

```sh
vagrant plugin list | grep vagrant-libvirt
```

## Running it

From the repository root:

```sh
# Both FreeBSD and OpenBSD, one after another
./scripts/run-bsd-ci.sh

# Just one machine
./scripts/run-bsd-ci.sh freebsd
./scripts/run-bsd-ci.sh openbsd

# Tear the VM down again after the run (default: leave it running so you
# can `vagrant ssh` in and iterate without re-provisioning each time)
./scripts/run-bsd-ci.sh --destroy freebsd
```

Each run:

1. `vagrant up <machine> --provider=libvirt` -- boots the box and runs
   the one-time provisioning script (`scripts/vagrant/provision-*.sh`),
   which installs the build tooling and prints the detected base
   compiler version.
2. `vagrant rsync <machine>` -- syncs the working tree into `/vagrant`
   inside the guest (`.git/`, `build*/`, and `.vagrant/` are excluded).
3. `vagrant ssh <machine> -c '... scripts/run-native-ci.sh ...'` --
   configures, builds, and runs the test suite plus the public-header
   compile check with the guest's native `cc`/`c++`, mirroring
   `scripts/run-native-ci.sh`'s use on Linux.

VM size defaults to 4 vCPUs / 4096 MiB; override with the
`RELOCO_BSD_VM_CPUS`/`RELOCO_BSD_VM_MEMORY` environment variables before
`vagrant up` if your host needs different sizing.

## Iterating and debugging

The VM is left running by default. To re-run after editing source:

```sh
vagrant rsync freebsd
vagrant ssh freebsd -c 'cd /vagrant && bash scripts/run-native-ci.sh build-ci-freebsd'
```

To get an interactive shell:

```sh
vagrant ssh freebsd
```

## Tearing down

```sh
vagrant destroy -f freebsd openbsd
```

or pass `--destroy` to `scripts/run-bsd-ci.sh` to do this automatically
at the end of a run.
