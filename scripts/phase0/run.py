#!/usr/bin/env python3
"""Run the CTest inventory without hiding crashes, skips, or known defects.

Uses CTest's resolved commands/properties, preserving WILL_FAIL for ordinary exit
codes only. Each test is isolated in a child process and has its own output file.
No game paths are accepted or discovered. Standard library only, Python >= 3.10.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent.parent


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def query(command, cwd=SOURCE):
    try:
        p = subprocess.run(command, cwd=cwd, capture_output=True, text=True, errors="replace", timeout=60)
        return {"command": command, "returncode": p.returncode, "stdout": p.stdout, "stderr": p.stderr}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"command": command, "error": str(error)}


def properties(test):
    return {item["name"]: item["value"] for item in test.get("properties", [])}


def expected_names(is_windows):
    data = json.loads((HERE / "inventory.json").read_text())
    return set(data["original"] + data["phase0"] + data.get("phase1", []) + data.get("phase2", []) + data.get("phase3", []) + data.get("phase4a", []) + data.get("phase4", []) + (data["windows_only"] if is_windows else []))


def verify_inventory(tests, is_windows):
    names = [test["name"] for test in tests]
    if len(names) != len(set(names)):
        raise ValueError("Duplicate CTest registrations")
    expected = expected_names(is_windows)
    if set(names) != expected:
        raise ValueError(f"Inventory changed: missing={sorted(expected-set(names))}, added={sorted(set(names)-expected)}")
    for test in tests:
        if not test.get("command") or not properties(test).get("LABELS"):
            raise ValueError(f"Missing command or classification: {test['name']}")


def classify(returncode, timed_out, will_fail):
    if timed_out:
        return "timeout"
    # POSIX signals and Windows NTSTATUS exceptions must never satisfy WILL_FAIL.
    if returncode is None:
        return "missing_executable"
    if returncode < 0 or returncode >= 0x80000000:
        return "crash"
    return "pass" if ((returncode != 0) if will_fail else (returncode == 0)) else "fail"


def matching_known(result, output, known, host):
    for item in known:
        if (item["test"] == result["name"] and item["platform"] == host
                and item["status"] == result["status"] and item["returncode"] == result["returncode"]
                and re.search(item["signature"], output)):
            return item["id"]
    return None


def unexpected_failures(results):
    return [r["name"] for r in results if r["status"] not in ("pass", "unavailable") and not r.get("known_failure")]


def source_manifest():
    files = subprocess.check_output(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=SOURCE).decode().split("\0")
    hashes = {}
    for name in sorted(set(files) - {""}):
        path = SOURCE / name
        if path.is_file():
            hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    digest = hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()
    return {"sha256": digest, "files": hashes}


def run_test(test, output_dir, default_timeout, env):
    name = test["name"]
    props = properties(test)
    command = test["command"]
    log = output_dir / f"{name}.log"
    result = {"name": name, "command": command, "labels": props["LABELS"], "log": log.name,
              "known_failure": None, "returncode": None, "timeout_seconds": props.get("TIMEOUT", default_timeout)}
    start = time.monotonic()
    timeout = False
    try:
        with log.open("wb") as out:
            kwargs = {"start_new_session": True} if os.name != "nt" else {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
            with subprocess.Popen(command, cwd=props.get("WORKING_DIRECTORY", str(output_dir)), stdout=out, stderr=subprocess.STDOUT, env=env, **kwargs) as child:
                try:
                    result["returncode"] = child.wait(timeout=float(result["timeout_seconds"]))
                except subprocess.TimeoutExpired:
                    timeout = True
                    if os.name == "nt":
                        try:
                            subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2)
                        except subprocess.TimeoutExpired:
                            pass
                        # Restricted Windows tokens can deny taskkill even for
                        # our child; the Popen process handle still permits termination.
                        if child.poll() is None:
                            child.kill()
                    else:
                        os.killpg(child.pid, signal.SIGKILL)
                    child.wait(timeout=5)
                    result["returncode"] = child.returncode
    except OSError as error:
        log.write_text(str(error), encoding="utf-8")
    result["duration_seconds"] = round(time.monotonic() - start, 3)
    result["status"] = classify(result["returncode"], timeout, props.get("WILL_FAIL", False))
    result["output_sha256"] = hashlib.sha256(log.read_bytes()).hexdigest()
    text = log.read_text(encoding="utf-8", errors="replace")
    unavailable = [line.removeprefix("PHASE0_UNAVAILABLE ") for line in text.splitlines()
                   if line.startswith("PHASE0_UNAVAILABLE ")]
    if result["returncode"] == 77 and not timeout and "gpu" in result["labels"] and unavailable:
        result["status"] = "unavailable"
        result["reason"] = unavailable[0]
    result["observations"] = []
    for line in text.splitlines():
        if line.startswith("PHASE0 "):
            try:
                result["observations"].append(json.loads(line[7:]))
            except json.JSONDecodeError:
                result["observations"].append({"invalid_json": line})
    return result, text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--gpu", choices=("required", "unavailable"), required=True)
    parser.add_argument("--gpu-reason")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--known-failures", type=Path, default=HERE / "known-failures.json")
    args = parser.parse_args()
    if args.gpu == "unavailable" and not args.gpu_reason:
        parser.error("--gpu unavailable requires an explicit --gpu-reason")
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)  # Never overwrite evidence from an earlier run.
    inventory_query = query(["ctest", "--test-dir", str(build), "--show-only=json-v1"])
    write_json(output / "inventory-query.json", inventory_query)
    if inventory_query.get("returncode") != 0:
        raise RuntimeError("CTest inventory query failed; see inventory-query.json")
    inventory = json.loads(inventory_query["stdout"])
    write_json(output / "inventory.json", inventory)
    tests = inventory["tests"]
    verify_inventory(tests, os.name == "nt")
    host = "windows" if os.name == "nt" else "linux" if sys.platform.startswith("linux") else sys.platform
    known = json.loads(args.known_failures.read_text())["failures"]
    env = os.environ.copy()
    env.update(VK_LOADER_LAYERS_DISABLE="~implicit~", SDL_AUDIODRIVER="dummy")
    # Keep filesystem fixtures within the run's writable, canonical workspace.
    # A host TEMP path may have different access/reparse behavior under a
    # restricted test token. Preserve this choice in the evidence manifest.
    temporary = output / "tmp"
    temporary.mkdir()
    env.update(TEMP=str(temporary), TMP=str(temporary), TMPDIR=str(temporary))
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    tools = {}
    for executable in ("cmake", "ctest", "ninja", "clang-cl" if os.name == "nt" else "clang++-18", "glslangValidator", "git", "vulkaninfo"):
        path = shutil.which(executable)
        tools[executable] = {"path": path, "version": query([path, "--summary" if executable == "vulkaninfo" else "--version"]) if path else {"unavailable": True}}
    manifest = {"schema": 1, "utc": dt.datetime.now(dt.timezone.utc).isoformat(), "host": host,
                "os": platform.platform(), "machine": platform.machine(), "python": sys.version,
                "source": str(SOURCE), "build": str(build), "revision": query(["git", "rev-parse", "HEAD"]),
                "status": query(["git", "status", "--porcelain=v1"]), "source_content": source_manifest(),
                "submodules": query(["git", "submodule", "status", "--recursive"]),
                "gitlinks": query(["git", "ls-files", "--stage", "3rdparty"]),
                "tools": tools, "gpu_mode": args.gpu, "gpu_reason": args.gpu_reason,
                "environment_overrides": {key: env.get(key) for key in ("TEMP", "TMP", "TMPDIR", "VK_LOADER_LAYERS_DISABLE", "SDL_AUDIODRIVER", "VK_LAYER_PATH", "VK_LAYER_VALIDATE_SYNC", "PROSPEROX_VULKAN_DEVICE", "PROSPEROX_VULKAN_VALIDATION")},
                "seed": "0x505830", "known_failures_sha256": hashlib.sha256(args.known_failures.read_bytes()).hexdigest()}
    if os.name == "nt":
        manifest["hardware"] = query(["powershell", "-NoProfile", "-Command",
            "[pscustomobject]@{CPU=(Get-CimInstance Win32_Processor).Name;RAM=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory}|ConvertTo-Json"])
    else:
        manifest["hardware"] = {"cpu": query(["lscpu", "-J"]), "memory": Path("/proc/meminfo").read_text() if Path("/proc/meminfo").exists() else "unavailable"}
    cache = build / "CMakeCache.txt"
    if cache.exists():
        shutil.copyfile(cache, output / "CMakeCache.txt")
    manifest["prebuilt_archives"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (build / "externals").glob("*.zip")}
    manifest["fetch_revisions"] = {p.name: query(["git", "-C", str(p), "rev-parse", "HEAD"]) for p in (build / "_deps").glob("*-src") if (p / ".git").exists()}
    write_json(output / "manifest.json", manifest)
    results = []
    for test in tests:
        if "gpu" in properties(test)["LABELS"] and args.gpu == "unavailable":
            result = {"name": test["name"], "labels": properties(test)["LABELS"], "command": test["command"],
                      "status": "unavailable", "reason": args.gpu_reason, "known_failure": None}
        else:
            result, text = run_test(test, output, args.timeout, env)
            if result["status"] != "pass":
                result["known_failure"] = matching_known(result, text, known, host)
        results.append(result)
        print(f"{result['name']}: {result['status']}" + (f" [{result['known_failure']}]" if result.get("known_failure") else ""), flush=True)
        write_json(output / "results.json", results)
    unexpected = unexpected_failures(results)
    observations = [item for r in results for item in r.get("observations", [])]
    gpu_observed = any(item.get("probe") == "eop_visibility" and item.get("retired_after_release") is True
                       and item.get("gpu_readback") == 0x505830 for item in observations)
    spirv_observed = any(item.get("probe") == "spirv_validation" and item.get("validated") is True for item in observations)
    if args.gpu == "required" and not gpu_observed:
        unexpected.append("__missing_gpu_execution_evidence")
    if not spirv_observed:
        unexpected.append("__missing_spirv_validation_evidence")
    counts = {status: sum(r["status"] == status for r in results) for status in sorted({r["status"] for r in results})}
    summary = {"schema": 1, "counts": counts, "unexpected_failures": unexpected,
               "baseline_gate_passed": not unexpected, "all_correctness_tests_passed": all(r["status"] == "pass" for r in results),
               "known_failures": [r["name"] for r in results if r.get("known_failure")],
               "inventory_count": len(results), "gpu_execution_required": args.gpu == "required",
               "gpu_execution_observed": gpu_observed, "spirv_validation_observed": spirv_observed}
    write_json(output / "summary.json", summary)
    print(json.dumps(summary, indent=2))
    return 1 if unexpected else 0


if __name__ == "__main__":
    sys.exit(main())
