#!/usr/bin/env python3
"""Compare real-container workloads; never accept failed or incomplete samples."""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import io
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys

SIZES = (0, 1, 8, 9, 16, 17, 24, 25, 64, 65, 256, 1024, 8192)
CASES = {(t, m, n) for t in ("u64", "record64")
         for m in ("cold", "reserved", "reuse") for n in SIZES}
FIELDS = ("policy", "type", "mode", "count", "repeats", "elapsed_ms",
          "checksum", "capacity", "reserved_bytes", "growths")


def parse_samples(text: str, policy: str) -> dict:
    reader = csv.DictReader(io.StringIO(text))
    if tuple(reader.fieldnames or ()) != FIELDS:
        raise ValueError("Unexpected CSV schema")
    result = {}
    for raw in reader:
        if None in raw or any(v is None for v in raw.values()):
            raise ValueError("Malformed CSV row")
        if raw["policy"] != policy:
            raise ValueError("Wrong compiled policy")
        row = dict(raw)
        for field in ("count", "repeats", "checksum", "capacity", "reserved_bytes", "growths"):
            row[field] = int(raw[field])
            if row[field] < 0:
                raise ValueError("Negative integer metric")
        row["elapsed_ms"] = float(raw["elapsed_ms"])
        if not math.isfinite(row["elapsed_ms"]) or row["elapsed_ms"] <= 0:
            raise ValueError("Invalid measured duration")
        n, repeats = row["count"], row["repeats"]
        if repeats != max(16, min(32768, 262144 // (n + 1))):
            raise ValueError("Unexpected repeat count")
        if row["checksum"] != n * repeats * (n + repeats) // 2:
            raise ValueError("Incorrect workload checksum")
        if row["capacity"] < n:
            raise ValueError("Capacity below size")
        width = {"u64": 8, "record64": 64}.get(row["type"])
        if width is None or row["reserved_bytes"] != row["capacity"] * width:
            raise ValueError("Incorrect reserved byte count")
        if row["mode"] != "cold" and row["growths"] != 0:
            raise ValueError("Unexpected allocation during retained/reserved workload")
        key = row["type"], row["mode"], n
        if key in result:
            raise ValueError("Duplicate case")
        result[key] = row
    if set(result) != CASES:
        raise ValueError("Missing or unexpected workload cases")
    return result


def summarize(rows: list[dict], pairs: int) -> list[dict]:
    groups = {}
    for row in rows:
        key = row["type"], row["mode"], row["count"]
        item = groups.setdefault(key, {})
        slot = row["pair"], row["variant"]
        if slot in item:
            raise ValueError("Duplicate measured pair")
        item[slot] = row
    if set(groups) != CASES:
        raise ValueError("Incomplete case set")
    summary = []
    expected_slots = {(p, v) for p in range(pairs) for v in ("baseline", "candidate")}
    for key, samples in sorted(groups.items()):
        if set(samples) != expected_slots:
            raise ValueError("Incomplete pair set")
        base = [samples[p, "baseline"] for p in range(pairs)]
        candidate = [samples[p, "candidate"] for p in range(pairs)]
        for b, c in zip(base, candidate):
            if (b["checksum"], b["repeats"]) != (c["checksum"], c["repeats"]):
                raise ValueError("Work differs across variants")
        ratios = [c["elapsed_ms"] / b["elapsed_ms"] for b, c in zip(base, candidate)]
        bm = statistics.median(x["elapsed_ms"] for x in base)
        cm = statistics.median(x["elapsed_ms"] for x in candidate)
        faster = sum(x < 1 for x in ratios)
        flags = []
        if min(bm, cm) < 5:
            flags.append("short_batch")
        if 0 < faster < pairs:
            flags.append("mixed_pair_direction")
        summary.append(dict(type=key[0], mode=key[1], count=key[2], pairs=pairs,
                            baseline_median_ms=bm, candidate_median_ms=cm,
                            paired_median_change_percent=100 * (statistics.median(ratios) - 1),
                            faster_pairs=faster,
                            baseline_capacity=base[0]["capacity"],
                            candidate_capacity=candidate[0]["capacity"],
                            baseline_reserved_bytes=base[0]["reserved_bytes"],
                            candidate_reserved_bytes=candidate[0]["reserved_bytes"],
                            baseline_growths=base[0]["growths"],
                            candidate_growths=candidate[0]["growths"],
                            cautions=";".join(flags)))
    return summary


def write_csv(path: Path, rows: list[dict]) -> None:
    if not rows:
        return
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def binary_info(path: Path) -> dict:
    return {"path": str(path), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def invoke(executable: Path, argument: str, log: Path) -> str:
    try:
        result = subprocess.run([str(executable), argument], capture_output=True,
                                text=True, timeout=180, check=False)
    except subprocess.TimeoutExpired as error:
        log.with_suffix(".error.txt").write_text(str(error), encoding="utf-8")
        raise RuntimeError("Workload timed out") from error
    log.with_suffix(".stdout.txt").write_text(result.stdout, encoding="utf-8")
    log.with_suffix(".stderr.txt").write_text(result.stderr, encoding="utf-8")
    if result.returncode != 0:
        raise RuntimeError(f"{executable.name} failed: {result.returncode}")
    return result.stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--pairs", type=int, default=12)
    parser.add_argument("--output", type=Path, default=Path("rsx-validation-results"))
    args = parser.parse_args()
    if args.pairs < 4 or args.pairs % 2:
        parser.error("--pairs must be even and at least four")
    binaries = {"baseline": args.baseline.resolve(strict=True),
                "candidate": args.candidate.resolve(strict=True)}
    args.output.mkdir(parents=True, exist_ok=True)
    logs = args.output / "logs"
    logs.mkdir(exist_ok=True)
    metadata = {"created_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
                "git_sha": os.environ.get("GITHUB_SHA"),
                "run_id": os.environ.get("GITHUB_RUN_ID"),
                "os": platform.platform(), "cpu": platform.processor(),
                "logical_cpus": os.cpu_count(), "python": sys.version,
                "runner_image": os.environ.get("ImageVersion"),
                "compiler_flags": os.environ.get("BENCH_CXX_FLAGS"),
                "binaries": {k: binary_info(v) for k, v in binaries.items()},
                "pairs": args.pairs, "order": "AB then BA, balanced",
                "scope": "Container construction/fill/consume; not emulator FPS or peak RSS",
                "status": "running"}
    meta_path = args.output / "metadata.json"
    meta_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    rows = []
    try:
        for variant, path in binaries.items():
            policy = "baseline" if variant == "baseline" else "grow25"
            checks = invoke(path, "--verify-only", logs / (variant + "-verify"))
            if f"PASS boundary/content/alignment checks policy={policy}" not in checks:
                raise RuntimeError("Policy verification result absent")
            parse_samples(invoke(path, "--benchmark", logs / (variant + "-warmup")), policy)
        for pair in range(args.pairs):
            order = ("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline")
            for variant in order:
                text = invoke(binaries[variant], "--benchmark", logs / f"{pair:02d}-{variant}")
                samples = parse_samples(text, "baseline" if variant == "baseline" else "grow25")
                rows.extend(dict(pair=pair, variant=variant, **sample) for sample in samples.values())
            # Preserve raw completed pairs even if a later process fails.
            write_csv(args.output / "samples.csv", rows)
        summary = summarize(rows, args.pairs)
        write_csv(args.output / "summary.csv", summary)
        for row in summary:
            if row["count"] in (8, 25, 256, 8192):
                print(f'{row["type"]} {row["mode"]} n={row["count"]}: '
                      f'{row["paired_median_change_percent"]:.2f}% '
                      f'faster_pairs={row["faster_pairs"]}/{args.pairs} '
                      f'capacity={row["baseline_capacity"]}->{row["candidate_capacity"]} '
                      f'cautions={row["cautions"] or "none"}')
        metadata["status"] = "success"
    except Exception as error:
        metadata["status"] = "failure"
        metadata["error"] = str(error)
        raise
    finally:
        meta_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
