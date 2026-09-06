#!/usr/bin/env python3
"""Capture and qualify Shot Stopper P2 heap/timing soak evidence."""

from __future__ import annotations

import argparse
import json
import os
import signal
import statistics
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


COUNTERS = (
    "loopDeadlineMisses",
    "scaleWorkerDeadlineMisses",
    "bleHostAllocFallback",
    "hciRxDropped",
    "hciTxDropped",
)


def number(source: dict[str, Any], key: str) -> float | None:
    value = source.get(key)
    return float(value) if isinstance(value, (int, float)) else None


def linear_slope_per_hour(values: list[float], interval_s: float) -> float:
    if len(values) < 2 or interval_s <= 0:
        return 0.0
    xs = [index * interval_s / 3600.0 for index in range(len(values))]
    x_mean = statistics.fmean(xs)
    y_mean = statistics.fmean(values)
    denominator = sum((x - x_mean) ** 2 for x in xs)
    if denominator == 0:
        return 0.0
    return sum((x - x_mean) * (y - y_mean) for x, y in zip(xs, values)) / denominator


def analyze(records: list[dict[str, Any]], args: argparse.Namespace) -> dict[str, Any]:
    payloads = [record["payload"] for record in records if "payload" in record]
    errors = [record for record in records if "error" in record]
    health = [payload.get("health", {}) for payload in payloads]
    failures: list[str] = []
    metrics: dict[str, Any] = {"samples": len(payloads), "fetchErrors": len(errors)}

    if len(payloads) < args.min_samples:
        failures.append(f"only {len(payloads)} valid samples; need {args.min_samples}")
    if errors:
        failures.append(f"{len(errors)} snapshot fetch/decode errors")

    uptimes = [value for item in health if (value := number(item, "uptimeMs")) is not None]
    reboot_count = sum(right < left for left, right in zip(uptimes, uptimes[1:]))
    metrics["reboots"] = reboot_count
    if reboot_count:
        failures.append(f"{reboot_count} uptime regression(s)/reboot(s)")

    stale_count = sum(item.get("snapshotStale") is True for item in health)
    metrics["staleSnapshots"] = stale_count
    if stale_count:
        failures.append(f"{stale_count} stale control snapshot(s)")

    for key in COUNTERS:
        values = [value for item in health if (value := number(item, key)) is not None]
        if not values:
            continue
        delta = max(values) - min(values)
        metrics[key + "Delta"] = delta
        if delta > 0:
            failures.append(f"{key} increased by {delta:g}")

    for key, minimum in (
        ("freeHeapBytes", args.min_internal_free),
        ("largestFreeHeapBlockBytes", args.min_internal_largest),
    ):
        values = [value for item in health if (value := number(item, key)) is not None]
        if not values:
            failures.append(f"missing {key}")
            continue
        observed_min = min(values)
        metrics[key + "Minimum"] = observed_min
        if observed_min < minimum:
            failures.append(f"{key} minimum {observed_min:g} < {minimum}")

    largest = [
        value
        for item in health
        if (value := number(item, "largestFreeHeapBlockBytes")) is not None
    ]
    if len(largest) >= 2:
        delta = largest[-1] - largest[0]
        slope = linear_slope_per_hour(largest, args.interval)
        metrics["largestBlockFirstLastDeltaBytes"] = delta
        metrics["largestBlockSlopeBytesPerHour"] = slope
        if delta < -args.max_largest_drop and slope < 0:
            failures.append(
                "internal largest block has sustained negative trend "
                f"(delta={delta:g}, slope={slope:.1f} B/h)"
            )

    psram_largest = [
        value
        for item in health
        if (value := number(item, "psramLargestFreeBlockBytes")) is not None
        and value > 0
    ]
    psram_free = [
        value
        for item in health
        if (value := number(item, "psramFreeBytes")) is not None and value > 0
    ]
    if psram_free:
        metrics["psramFreeBytesMinimum"] = min(psram_free)
    if len(psram_largest) >= 2:
        delta = psram_largest[-1] - psram_largest[0]
        slope = linear_slope_per_hour(psram_largest, args.interval)
        metrics["psramLargestFirstLastDeltaBytes"] = delta
        metrics["psramLargestSlopeBytesPerHour"] = slope
        if delta < -args.max_psram_largest_drop and slope < 0:
            failures.append(
                "PSRAM largest block has sustained negative trend "
                f"(delta={delta:g}, slope={slope:.1f} B/h)"
            )

    live_clients: list[float] = []
    worker_starts: list[float] = []
    task_counts: list[int] = []
    for payload in payloads:
        webhook = payload.get("webhooks", {})
        if isinstance(webhook, dict):
            created = number(webhook, "clientCreates")
            cleaned = number(webhook, "clientCleanups")
            if created is not None and cleaned is not None:
                live_clients.append(created - cleaned)
            starts = number(webhook, "workerStarts")
            if starts is not None:
                worker_starts.append(starts)
        else:
            failures.append("missing webhooks lifecycle snapshot")
        rows = payload.get("tasks", {}).get("rows", [])
        if isinstance(rows, list) and rows:
            task_counts.append(len(rows))
    if live_clients:
        metrics["webhookLiveClientMaximum"] = max(live_clients)
        if min(live_clients) < 0 or max(live_clients) > 1:
            failures.append(
                f"webhook live HTTP handles out of bound: {min(live_clients):g}..{max(live_clients):g}"
            )
    else:
        failures.append("missing webhook client create/cleanup counters")
    if worker_starts:
        starts_delta = max(worker_starts) - min(worker_starts)
        metrics["webhookWorkerStartsDelta"] = starts_delta
        if starts_delta > 1:
            failures.append(
                f"webhook worker restarted {starts_delta:g} times during soak"
            )
    else:
        failures.append("missing webhook worker lifecycle counters")
    if task_counts:
        metrics["profiledTaskCountRange"] = [min(task_counts), max(task_counts)]

    stack_values: list[float] = []
    for item in health:
        for key in ("loopStackMinWords", "scaleStackMinWords", "bleRuntimeHostStackMinWords"):
            value = number(item, key)
            if value is not None and value > 0:
                stack_values.append(value)
    for payload in payloads:
        rows = payload.get("tasks", {}).get("rows", [])
        if isinstance(rows, list):
            for row in rows:
                if isinstance(row, dict):
                    value = number(row, "stackMinWords")
                    if value is not None and value > 0:
                        stack_values.append(value)
    if stack_values:
        metrics["stackMinimumWords"] = min(stack_values)
        if min(stack_values) < args.min_stack_words:
            failures.append(
                f"stack watermark {min(stack_values):g} < {args.min_stack_words} words"
            )
    else:
        metrics["stackMinimumWords"] = None

    return {
        "schemaVersion": 1,
        "scenario": args.scenario,
        "passed": not failures,
        "metrics": metrics,
        "limits": {
            "minSamples": args.min_samples,
            "minInternalFreeBytes": args.min_internal_free,
            "minInternalLargestBytes": args.min_internal_largest,
            "maxLargestFirstLastDropBytes": args.max_largest_drop,
            "maxPsramLargestFirstLastDropBytes": args.max_psram_largest_drop,
            "minStackWords": args.min_stack_words,
        },
        "failures": failures,
    }


def load_headers(env_name: str) -> dict[str, str]:
    raw = os.environ.get(env_name, "")
    if not raw:
        return {}
    decoded = json.loads(raw)
    if not isinstance(decoded, dict) or not all(
        isinstance(key, str) and isinstance(value, str)
        for key, value in decoded.items()
    ):
        raise ValueError(f"{env_name} must be a JSON object of string headers")
    return decoded


def fetch(url: str, headers: dict[str, str], timeout: float) -> dict[str, Any]:
    request = urllib.request.Request(url, headers=headers, method="GET")
    with urllib.request.urlopen(request, timeout=timeout) as response:
        payload = json.load(response)
    if not isinstance(payload, dict):
        raise ValueError("status response is not a JSON object")
    return payload


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--url", help="Full diagnostic/admin status URL")
    result.add_argument("--output", type=Path, help="JSONL evidence path")
    result.add_argument("--scenario", default="combined")
    result.add_argument("--duration", type=float, default=8 * 3600)
    result.add_argument("--interval", type=float, default=5.0)
    result.add_argument("--timeout", type=float, default=4.0)
    result.add_argument("--headers-env", default="SHOTSTOPPER_SOAK_HEADERS")
    result.add_argument("--min-samples", type=int, default=60)
    result.add_argument("--min-internal-free", type=int, default=48 * 1024)
    result.add_argument("--min-internal-largest", type=int, default=16 * 1024)
    result.add_argument("--max-largest-drop", type=int, default=16 * 1024)
    result.add_argument("--max-psram-largest-drop", type=int, default=64 * 1024)
    result.add_argument("--min-stack-words", type=int, default=384)
    result.add_argument("--self-test", action="store_true")
    return result


def self_test(args: argparse.Namespace) -> int:
    args.min_samples = 2
    healthy = {
        "health": {
            "uptimeMs": 1000,
            "snapshotStale": False,
            "loopDeadlineMisses": 0,
            "scaleWorkerDeadlineMisses": 0,
            "freeHeapBytes": 100000,
            "largestFreeHeapBlockBytes": 60000,
            "bleRuntimeHostStackMinWords": 600,
        },
        "webhooks": {
            "workerStarts": 1,
            "clientCreates": 1,
            "clientCleanups": 0,
        },
    }
    records = [{"payload": healthy}, {"payload": json.loads(json.dumps(healthy))}]
    records[1]["payload"]["health"]["uptimeMs"] = 2000
    assert analyze(records, args)["passed"]
    records[1]["payload"]["health"]["loopDeadlineMisses"] = 1
    assert not analyze(records, args)["passed"]
    return 0


def main() -> int:
    args = parser().parse_args()
    if args.self_test:
        return self_test(args)
    if not args.url or args.output is None:
        parser().error("--url and --output are required unless --self-test is used")
    if args.duration <= 0 or args.interval <= 0 or args.timeout <= 0:
        parser().error("duration, interval and timeout must be positive")

    try:
        headers = load_headers(args.headers_env)
    except (ValueError, json.JSONDecodeError) as error:
        print(str(error), file=sys.stderr)
        return 2

    args.output.parent.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, Any]] = []
    stopped = False

    def stop_capture(_signum: int, _frame: Any) -> None:
        nonlocal stopped
        stopped = True

    signal.signal(signal.SIGINT, stop_capture)
    signal.signal(signal.SIGTERM, stop_capture)
    started = time.monotonic()
    next_sample = started
    with args.output.open("w", encoding="utf-8") as evidence:
        while not stopped and time.monotonic() - started < args.duration:
            captured = time.time()
            record: dict[str, Any] = {
                "schemaVersion": 1,
                "capturedUnixSec": captured,
                "elapsedSec": time.monotonic() - started,
                "scenario": args.scenario,
            }
            try:
                record["payload"] = fetch(args.url, headers, args.timeout)
            except (OSError, ValueError, urllib.error.URLError, json.JSONDecodeError) as error:
                record["error"] = f"{type(error).__name__}: {error}"
            records.append(record)
            evidence.write(json.dumps(record, separators=(",", ":")) + "\n")
            evidence.flush()
            next_sample += args.interval
            delay = next_sample - time.monotonic()
            if delay > 0:
                time.sleep(delay)

    summary = analyze(records, args)
    summary["durationSec"] = time.monotonic() - started
    summary["evidence"] = str(args.output)
    summary_path = args.output.with_suffix(args.output.suffix + ".summary.json")
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
