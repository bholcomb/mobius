#!/usr/bin/env python3
"""Run the comprehensive benchmark across Mobius, Lua, and CPython.

Each benchmark script prints one line per benchmark:

    BENCH <id> <seconds> <checksum>

This runner executes each implementation N times, takes the *median* per
benchmark (means are skewed by scheduler noise and by the first run's cold
caches), and cross-checks the integer checksums.

The checksum check is the important part. Before this existed, the three
scripts silently computed different values: Mobius `/` truncates and `%` is a
C-style remainder, while Lua and Python floor both, so any negative
intermediate made the comparison meaningless. If checksums disagree, this
script reports the mismatch and exits non-zero rather than printing timings
that do not describe the same computation.

Usage:
    python3 benchmarks/run_benchmarks.py [--runs 5] [--lua PATH] [--json OUT]
    python3 benchmarks/run_benchmarks.py --only mobius   # skip lua/python
"""

import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Display order and human-readable labels for the benchmark ids.
LABELS = [
    ("arith",   "Arithmetic (integer)"),
    ("fib",     "Recursive calls (fib 30)"),
    ("array",   "Array ops (dense numeric)"),
    ("table",   "Table ops (string-key map)"),
    ("string",  "String ops"),
    ("nested",  "Nested loops"),
    ("objlife", "Object create / destroy"),
    ("mixed",   "Mixed workload"),
    ("total",   "Total"),
]


def find_mobius():
    for candidate in (os.path.join(ROOT, "bin", "mobius"),
                      os.path.join(ROOT, "build", "linux-x86_64-release", "bin", "mobius")):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def parse_output(text):
    """Return {bench_id: (seconds, checksum)} from BENCH lines."""
    results = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[0] == "BENCH":
            _, bench_id, secs, checksum = parts
            results[bench_id] = (float(secs), int(checksum))
    return results


def run_once(cmd, cwd):
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(
            f"{' '.join(cmd)} exited {proc.returncode}\n"
            f"--- stdout ---\n{proc.stdout}\n--- stderr ---\n{proc.stderr}"
        )
    results = parse_output(proc.stdout)
    if not results:
        raise RuntimeError(f"{' '.join(cmd)} produced no BENCH lines:\n{proc.stdout}")
    return results


def run_impl(name, cmd, runs, cwd):
    print(f"  {name}: ", end="", flush=True)
    timings, checksums = {}, {}
    for i in range(runs):
        results = run_once(cmd, cwd)
        for bench_id, (secs, checksum) in results.items():
            timings.setdefault(bench_id, []).append(secs)
            # Checksums must also be stable across runs of the same impl.
            if bench_id in checksums and checksums[bench_id] != checksum:
                raise RuntimeError(
                    f"{name} produced unstable checksum for '{bench_id}': "
                    f"{checksums[bench_id]} then {checksum}"
                )
            checksums[bench_id] = checksum
        print(".", end="", flush=True)
    print(" done")
    medians = {k: statistics.median(v) for k, v in timings.items()}
    return medians, checksums


def verify_checksums(all_checksums):
    """Return a list of human-readable mismatch descriptions (empty if OK)."""
    problems = []
    impls = list(all_checksums)
    bench_ids = {b for c in all_checksums.values() for b in c}
    for bench_id in sorted(bench_ids):
        if bench_id == "total":       # total's checksum is a placeholder 0
            continue
        seen = {impl: all_checksums[impl].get(bench_id) for impl in impls
                if bench_id in all_checksums[impl]}
        distinct = set(seen.values())
        if len(distinct) > 1:
            detail = ", ".join(f"{impl}={val}" for impl, val in seen.items())
            problems.append(f"  {bench_id}: {detail}")
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=5, help="runs per implementation (default 5)")
    ap.add_argument("--lua", default=None, help="path to the lua interpreter")
    ap.add_argument("--python", default=sys.executable, help="path to the python interpreter")
    ap.add_argument("--mobius", default=None, help="path to the mobius binary")
    ap.add_argument("--json", default=None, help="write results to this JSON file")
    ap.add_argument("--only", default=None, help="run only one impl: mobius|lua|python")
    args = ap.parse_args()

    mobius = args.mobius or find_mobius()
    lua = args.lua or shutil.which("lua") or shutil.which("lua5.4")

    impls = []
    if mobius:
        impls.append(("mobius", [mobius, os.path.join(HERE, "benchmark_comprehensive.mob")]))
    else:
        print("warning: mobius binary not found; build it with ./buildy -r", file=sys.stderr)
    if lua:
        impls.append(("lua", [lua, os.path.join(HERE, "benchmark_comprehensive.lua")]))
    else:
        print("warning: lua not found; pass --lua PATH to include it", file=sys.stderr)
    impls.append(("python", [args.python, os.path.join(HERE, "benchmark_comprehensive.py")]))

    if args.only:
        impls = [i for i in impls if i[0] == args.only]
        if not impls:
            sys.exit(f"error: --only {args.only} matched no available implementation")

    print(f"Running {args.runs} iterations per implementation (reporting medians)\n")
    all_medians, all_checksums = {}, {}
    for name, cmd in impls:
        medians, checksums = run_impl(name, cmd, args.runs, ROOT)
        all_medians[name] = medians
        all_checksums[name] = checksums

    # Semantic parity gate: refuse to print a comparison of different programs.
    if len(all_checksums) > 1:
        problems = verify_checksums(all_checksums)
        if problems:
            print("\nCHECKSUM MISMATCH — the implementations are not computing the "
                  "same thing, so their timings are not comparable:\n", file=sys.stderr)
            print("\n".join(problems), file=sys.stderr)
            sys.exit(1)
        print("\nChecksums agree across all implementations.")

    names = [n for n, _ in impls]
    baseline = "lua" if "lua" in all_medians else None

    header = f"\n{'Benchmark':<28}" + "".join(f"{n:>12}" for n in names)
    if baseline:
        header += f"{'mobius/lua':>13}"
    print(header)
    print("-" * len(header.strip("\n")))

    for bench_id, label in LABELS:
        if not any(bench_id in all_medians[n] for n in names):
            continue
        row = f"{label:<28}"
        for n in names:
            v = all_medians[n].get(bench_id)
            row += f"{v * 1000:>11.2f}ms" if v is not None else f"{'-':>12}"
        if baseline and bench_id in all_medians.get("mobius", {}) and bench_id in all_medians["lua"]:
            ratio = all_medians["mobius"][bench_id] / all_medians["lua"][bench_id]
            row += f"{ratio:>12.2f}x"
        print(row)

    if args.json:
        payload = {
            "runs": args.runs,
            "medians_seconds": all_medians,
            "checksums": {k: v for k, v in all_checksums.items()},
        }
        with open(args.json, "w") as fh:
            json.dump(payload, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print(f"\nWrote {args.json}")


if __name__ == "__main__":
    main()
