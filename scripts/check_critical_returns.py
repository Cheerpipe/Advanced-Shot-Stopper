#!/usr/bin/env python3
"""Reject newly discarded critical firmware return values (F-14/F-10)."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def reject(path: str, pattern: str, description: str) -> list[str]:
    source = (ROOT / path).read_text(encoding="utf-8")
    if re.search(pattern, source):
        return [f"{path}: discarded {description}"]
    return []


def main() -> int:
    failures: list[str] = []
    failures += reject(
        "shotStopper/ShotStopperBuzzer.h",
        r"\(void\)esp_timer_(?:stop|start_once)\s*\(",
        "buzzer timer result",
    )
    failures += reject(
        "shotStopper/ShotStopperOta.cpp",
        r"\(void\)esp_ota_abort\s*\(",
        "OTA abort result",
    )
    failures += reject(
        "libraries/EspressoScaleBLE/src/EspressoScaleBLENimble.cpp",
        r"\(void\)ble_gap_(?:disc_cancel|conn_cancel|terminate)\s*\(",
        "NimBLE teardown result",
    )

    watchdog = (ROOT / "shotStopper/ShotStopperWatchdog.h").read_text(
        encoding="utf-8"
    )
    if "TASK_WATCHDOG_OTA_TIMEOUT_MS" in watchdog or "TaskWatchdogOtaWindow" in watchdog:
        failures.append("ShotStopperWatchdog.h: obsolete global OTA TWDT window")

    for path in (ROOT / "shotStopper").rglob("*"):
        if path.suffix not in {".h", ".cpp"}:
            continue
        if path.name == "ShotStopperWatchdog.h":
            continue
        if "esp_task_wdt_reconfigure" in path.read_text(encoding="utf-8"):
            failures.append(f"{path.relative_to(ROOT)}: global TWDT reconfiguration outside initialization")

    if failures:
        for failure in failures:
            print(f"critical return error: {failure}", file=sys.stderr)
        return 1
    print("critical return policy: OTA, NimBLE, buzzer, NVS and TWDT checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
