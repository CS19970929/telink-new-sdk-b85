#!/usr/bin/env python3
"""Report how strongly host checks depend on production source text.

Batch A of the simplification keeps this number going down. It is an audit,
not a gate: exit code is always 0, and the numbers are printed for tracking.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main() -> int:
    files = sorted(glob.glob(os.path.join(ROOT, "tests", "*.py")))
    prod_files = []
    prod_calls = 0
    fixture_calls = 0
    text_assert_files = []
    text_asserts = 0
    for path in files:
        text = open(path, encoding="utf-8", errors="replace").read()
        prod = len(re.findall(r"\bfunction\(\s*(?!fixture)([A-Za-z_])", text))
        prod += len(re.findall(r"\bextract_function\(", text))
        fixture = len(re.findall(r"\bfunction\(\s*fixture", text))
        asserts = len(re.findall(r"\b(assertIn|assertNotIn|assertRegex)\(", text))
        if prod:
            prod_files.append((os.path.basename(path), prod))
            prod_calls += prod
        fixture_calls += fixture
        if asserts:
            text_assert_files.append((os.path.basename(path), asserts))
            text_asserts += asserts
    print("production-extraction files :", len(prod_files))
    print("production-extraction calls :", prod_calls)
    print("fixture-self extraction     :", fixture_calls)
    print("text-assertion files        :", len(text_assert_files))
    print("text-assertion calls        :", text_asserts)
    for name, count in sorted(prod_files, key=lambda item: -item[1]):
        print("  %-52s %d" % (name, count))
    return 0


if __name__ == "__main__":
    sys.exit(main())
