#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""Fixture runner for reloco-tidy.

Every tools/tidy/test/*.cpp fixture marks lines that must produce a diagnostic with
`// expect: reloco-check-name[, other-check]`. Any diagnostic on an unmarked line, and any
marked line without the diagnostic, fails the test. Fixtures live under /test/, which the
checks exempt by default, so the runner clears ExemptPaths. A `// checks: a, b` line restricts the run to those checks. A `// option: Key=Value` line sets a
check option (e.g. SafeCodeMode=opt-in) for that fixture.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

CHECKS = [
    "reloco-ref-qualified-member",
    "reloco-no-raw-pointer",
    "reloco-no-c-array",
    "reloco-implicit-conversion",
    "reloco-explicit-conversion",
    "reloco-must-use-result",
    "reloco-unused-result",
    "reloco-const-local",
    "reloco-no-shadow",
    "reloco-enum-switch",
    "reloco-banned-low-level",
    "reloco-mutable-global",
    "reloco-checked-operator",
    "reloco-safe-math",
    "reloco-member-init",
    "reloco-rule-of-five",
    "reloco-unsafe-call-scope",
    "reloco-noexcept",
    "reloco-out-param",
    "reloco-uninit-local",
    "reloco-implicit-copy",
    "reloco-lifetime-annotations",
    "reloco-pointer-annotation",
    "reloco-banned-std-type",
    "reloco-banned-construct",
    "reloco-borrow-check",
]
DIAG = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):\d+: (?:warning|error): .*\[(?P<check>reloco-[\w-]+)\]$")
ONLY = re.compile(r"//\s*checks:\s*(?P<names>[\w\-, ]+)")
OPTION = re.compile(r"//\s*option:\s*(?P<key>[\w.]+)=(?P<value>\S+)")
EXPECT = re.compile(r"//\s*expect:\s*(?P<names>[\w\-, ]+)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--clang-tidy", required=True)
    ap.add_argument("--plugin", required=True)
    ap.add_argument("--include", default="")
    ap.add_argument("fixtures", nargs="*")
    args = ap.parse_args()

    test_dir = Path(__file__).resolve().parent
    fixtures = [Path(f) for f in args.fixtures] or sorted(test_dir.glob("*.cpp"))
    failed = 0

    for fx in fixtures:
        expected = set()
        extra = {}
        only = []
        for no, text in enumerate(fx.read_text().splitlines(), 1):
            c = ONLY.search(text)
            if c:
                only = [n.strip() for n in c.group("names").split(",") if n.strip()]
            o = OPTION.search(text)
            if o:
                extra[o.group("key")] = o.group("value")
            m = EXPECT.search(text)
            if m:
                expected.update((no, n.strip()) for n in m.group("names").split(",") if n.strip())

        opts = {"ExemptPaths": "none", **extra}
        config = "{CheckOptions: {" + ", ".join(f"{c}.{k}: '{v}'" for c in CHECKS for k, v in opts.items()) + "}}"
        cmd = [args.clang_tidy, f"--load={args.plugin}", "--checks=-*," + (",".join(only) if only else "reloco-*"), f"--config={config}",
               "--quiet", str(fx), "--", "-std=c++20", "-fno-exceptions"]
        if args.include:
            cmd.append(f"-I{args.include}")
        proc = subprocess.run(cmd, capture_output=True, text=True)
        actual = set()
        for line in proc.stdout.splitlines():
            m = DIAG.match(line)
            if m and Path(m.group("file")).resolve() == fx.resolve():
                actual.add((int(m.group("line")), m.group("check")))

        missing, unexpected = sorted(expected - actual), sorted(actual - expected)
        if missing or unexpected or (proc.returncode not in (0, 1) and not actual and expected):
            failed += 1
            print(f"FAIL {fx.name}")
            for no, c in missing:
                print(f"  missing    {fx.name}:{no} {c}")
            for no, c in unexpected:
                print(f"  unexpected {fx.name}:{no} {c}")
            if proc.returncode not in (0, 1):
                print(proc.stderr[-2000:])
        else:
            print(f"ok   {fx.name} ({len(expected)} expectations)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
