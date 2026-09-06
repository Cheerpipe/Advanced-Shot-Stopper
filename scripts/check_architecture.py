#!/usr/bin/env python3
"""Enforce the F-20 firmware service boundaries without compiling sources."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def main() -> int:
    failures: list[str] = []

    safety_files = (
        "shotStopper/ShotStopperSafety.h",
        "shotStopper/ShotStopperMachineRelay.h",
        "shotStopper/ShotStopperHardwareTimer.h",
        "shotStopper/ShotStopperWatchdog.h",
        "shotStopper/ShotStopperResetGuard.h",
        "shotStopper/ShotStopperResetGuard.cpp",
    )
    forbidden_safety = (
        "ShotStopperNetwork",
        "ShotStopperWebhook",
        "<WiFi",
        "cJSON",
        "esp_http",
    )
    for relative in safety_files:
        source = text(relative)
        for token in forbidden_safety:
            if token in source:
                failures.append(f"SafetyKernel dependency {token!r} in {relative}")

    scale_source = text("shotStopper/ShotStopperScaleWorker.cpp")
    for token in ('#include "ShotStopperNetwork.h"', "networkManager."):
        if token in scale_source:
            failures.append(f"ScaleService bypasses its bridge via {token!r}")
    for required in (
        "ScaleWorkerBridgeCallbacks",
        "scaleWorkerBridge.syncNetworkRf",
        "configureScaleWorkerBridge",
    ):
        if required not in scale_source and required not in text(
            "shotStopper/ShotStopperScaleWorker.h"
        ):
            failures.append(f"ScaleService bridge contract missing {required!r}")

    network = text("shotStopper/ShotStopperNetwork.cpp") + text(
        "shotStopper/ShotStopperNetwork.h"
    )
    for token in (
        "machineRequestStart(",
        "machineRequestStop(",
        "closeRelayElectrical",
        "openRelayElectrical",
    ):
        if token in network:
            failures.append(f"NetworkService calls control implementation {token!r}")

    independent_harnesses = (
        "shotStopper/tests/safety_external_host_test.cpp",
        "shotStopper/tests/ota_state_concurrency_host_test.cpp",
        "shotStopper/tests/persistence_host_test.cpp",
        "shotStopper/tests/webhook_error_host_test.cpp",
        "shotStopper/tests/resource_owner_host_test.cpp",
        "libraries/EspressoScaleBLE/tests/scale_ble_portable_test.cpp",
    )
    for relative in independent_harnesses:
        if not (ROOT / relative).is_file():
            failures.append(f"independent service harness missing: {relative}")

    # Versioned ceilings keep new features from silently growing the legacy
    # roots. Reducing them is encouraged; increasing them requires an explicit
    # architecture review and must not be used to land feature code.
    line_ceilings = {
        "shotStopper/ShotStopperNetwork.cpp": 8750,
        "shotStopper/shotStopper.cpp": 6900,
        "shotStopper/ShotStopperDomain.h": 3300,
    }
    for relative, ceiling in line_ceilings.items():
        lines = len(text(relative).splitlines())
        if lines > ceiling:
            failures.append(f"legacy root grew beyond cap: {relative} {lines}>{ceiling}")

    if failures:
        for failure in failures:
            print(f"architecture error: {failure}", file=sys.stderr)
        return 1
    print("architecture boundaries: Safety/Control/Scale/Network/Persistence/Diagnostics OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
