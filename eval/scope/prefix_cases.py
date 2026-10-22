"""Writes a copy of a cases file with a phrase program put before every case's program.

  prefix_cases.py CASES PHRASES.ovis OUT

The case reader ends a case at a blank line, so the prefix's blank lines go
(blocks are by indentation, so the phrases read the same).
"""
import sys

cases, phrases, out = sys.argv[1:4]
prefix = [line for line in open(phrases).read().split("\n") if line.strip()]
result = []
for line in open(cases).read().split("\n"):
    result.append(line)
    if line.startswith("expect: "):
        result.extend(prefix)
open(out, "w").write("\n".join(result))
