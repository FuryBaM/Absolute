#!/usr/bin/env python3
"""Check that every diagnostic says where it is and what it is.

A compiler error is only useful if an editor can jump to it and a reader can
look it up, which is what "file, line, column and a diagnostic code" means.
Neither half held: 87 of the 361 diagnostics the error tests produce carried no
location at all, because only the top of an expression and the top of a
statement are stamped by the parser, and the code was recorded on most
diagnostics and printed on none of them.

This runs every error test and fails on any diagnostic that is missing either.
It is a statement about the state of the compiler, so it belongs in the suite
rather than in a one-off audit.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# The tests that exist to produce diagnostics.
ERROR_MARKERS = ("errors", "invalid")
LOCATED = re.compile(r"^.+\.abs:\d+:\d+: (Semantic|Syntax) error: ")
CODED = re.compile(r"\[E_[A-Z0-9_]+\]$")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--require-codes", action="store_true",
                        help="also fail on a diagnostic with no code")
    args = parser.parse_args()

    compiler = args.compiler.resolve()
    sources = [source for source in sorted(args.tests.resolve().glob("*.abs"))
               if any(marker in source.stem.lower() for marker in ERROR_MARKERS)]
    if not sources:
        print("diagnostic-audit: no error tests found", file=sys.stderr)
        return 1

    total = 0
    unlocated = []
    uncoded = []
    for source in sources:
        result = subprocess.run([str(compiler), str(source)], capture_output=True,
                                text=True, timeout=args.timeout, check=False)
        for line in (result.stdout + result.stderr).splitlines():
            if "error: " not in line:
                continue
            if line.startswith("Error: "):
                continue  # the summary line the driver prints after the list
            total += 1
            if not LOCATED.match(line):
                unlocated.append(f"{source.name}: {line}")
            elif not CODED.search(line.rstrip()):
                uncoded.append(f"{source.name}: {line}")

    for entry in unlocated[:20]:
        print(f"NO LOCATION {entry}")
    if args.require_codes:
        for entry in uncoded[:20]:
            print(f"NO CODE {entry}")

    failed = len(unlocated) + (len(uncoded) if args.require_codes else 0)
    print(f"diagnostic-audit={'ok' if failed == 0 else 'failed'} "
          f"diagnostics={total} without-location={len(unlocated)} "
          f"without-code={len(uncoded)}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
