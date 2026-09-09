#!/usr/bin/env python3
"""Compare repeated baseline outcomes; time and absolute build paths may differ."""
import argparse
import json
from pathlib import Path
import sys


def compare(first, second):
    def load(folder, name):
        return json.loads((folder / name).read_text(encoding="utf-8"))
    a, b = load(first, "manifest.json"), load(second, "manifest.json")
    errors = []
    for field in ("host", "seed", "gpu_mode", "prebuilt_archives", "known_failures_sha256"):
        if a[field] != b[field]:
            errors.append(f"manifest mismatch: {field}")
    # Compare code and test inputs even when evidence documentation was added later.
    def implementation(manifest):
        return {k: v for k, v in manifest["source_content"]["files"].items()
                if k.startswith(("src/", "tests/", "scripts/phase0/")) or k in ("CMakeLists.txt", "CMakePresets.json")}
    if implementation(a) != implementation(b):
        errors.append("implementation/test content changed")
    def outcomes(folder):
        return {r["name"]: (r["status"], r.get("returncode"), r.get("known_failure")) for r in load(folder, "results.json")}
    if outcomes(first) != outcomes(second):
        errors.append("test inventory or outcomes changed")
    return errors


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    args = parser.parse_args()
    errors = compare(args.first, args.second)
    print(json.dumps({"reproduced": not errors, "errors": errors}, indent=2))
    sys.exit(bool(errors))
