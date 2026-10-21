#!/usr/bin/env python3
"""Enforce the F-20 firmware service boundaries without compiling sources."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def text(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def service_text(root_file: str, directory: str) -> str:
    fragments = sorted((ROOT / directory).glob("*.inc"))
    return text(root_file) + "".join(
        fragment.read_text(encoding="utf-8") for fragment in fragments
    )


def main() -> int:
    failures: list[str] = []

    safety_files = (
        "src/OpenBrewSafety.h",
        "src/OpenBrewMachineRelay.h",
        "src/OpenBrewHardwareTimer.h",
        "src/OpenBrewWatchdog.h",
        "src/OpenBrewResetGuard.h",
        "src/OpenBrewResetGuard.cpp",
    )
    forbidden_safety = (
        "ShotStopperNetwork",
        "ShotStopperWebhook",
        "OpenBrewNetwork",
        "OpenBrewWebhook",
        "<WiFi",
        "cJSON",
        "esp_http",
    )
    for relative in safety_files:
        source = text(relative)
        for token in forbidden_safety:
            if token in source:
                failures.append(f"SafetyKernel dependency {token!r} in {relative}")

    scale_source = text("src/OpenBrewScaleWorker.cpp")
    for token in ('#include "OpenBrewNetwork.h"', "networkManager."):
        if token in scale_source:
            failures.append(f"ScaleService bypasses its bridge via {token!r}")
    for required in (
        "ScaleWorkerBridgeCallbacks",
        "scaleWorkerBridge.syncNetworkRf",
        "configureScaleWorkerBridge",
    ):
        if required not in scale_source and required not in text(
            "src/OpenBrewScaleWorker.h"
        ):
            failures.append(f"ScaleService bridge contract missing {required!r}")

    machine_common = (
        "src/machine/OpenBrewMachineIntegration.h",
        "src/machine/OpenBrewMachineIntegration.cpp",
        "src/machine/OpenBrewMachineBackflush.h",
    )
    for relative in machine_common:
        source = text(relative)
        for token in ("Micra", "Linea", "brewTarget", "ObservedMode", "Capabilities"):
            if token in source:
                failures.append(
                    f"common machine lifecycle exposes concrete feature {token!r} "
                    f"in {relative}"
                )
    machine_cmake = text("idf/components/openBrew/CMakeLists.txt")
    for required in (
        "OpenBrewLineaMicraIntegration.cpp",
        "OpenBrewMachineIntegration.cpp",
    ):
        if required not in machine_cmake:
            failures.append(f"selected machine source contract missing {required!r}")
    micra_service = text("src/machine/OpenBrewMicraService.cpp")
    for required in (
        "esp_crt_bundle_attach",
        "networkEligible",
        "parseJsonDocumentWithinLimits",
        "clearSession",
    ):
        if required not in micra_service:
            failures.append(f"Micra cloud boundary missing {required!r}")
    for forbidden in ("NimBLE", "ShotStopperBle", "OpenBrewBle", "ble_gap", "ble_gatt"):
        if forbidden in micra_service:
            failures.append(f"Micra cloud service depends on BLE via {forbidden!r}")
    entrypoints = text("src/platform/OpenBrewEntrypoints.inc")
    if "scaleBridgeOk && machineIntegrationOk" in entrypoints:
        failures.append("optional machine integration gates scale-worker startup")

    network = service_text("src/OpenBrewNetwork.cpp", "src/network") + text(
        "src/OpenBrewNetwork.h"
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
        "src/tests/safety_external_host_test.cpp",
        "src/tests/ota_state_concurrency_host_test.cpp",
        "src/tests/persistence_host_test.cpp",
        "src/tests/webhook_error_host_test.cpp",
        "src/tests/resource_owner_host_test.cpp",
        "src/tests/machine_integration_host_test.cpp",
        "src/tests/linea_micra_contract_host_test.cpp",
        "libraries/EspressoScaleBLE/tests/scale_ble_portable_test.cpp",
    )
    for relative in independent_harnesses:
        if not (ROOT / relative).is_file():
            failures.append(f"independent service harness missing: {relative}")

    # Versioned ceilings keep new features from silently growing the legacy
    # roots. Reducing them is encouraged; increasing them requires an explicit
    # architecture review and must not be used to land feature code.
    line_ceilings = {
        "src/OpenBrewNetwork.cpp": 2000,
        "src/openBrew.cpp": 2000,
        "src/OpenBrewDomain.h": 3300,
    }
    for relative, ceiling in line_ceilings.items():
        lines = len(text(relative).splitlines())
        if lines > ceiling:
            failures.append(f"legacy root grew beyond cap: {relative} {lines}>{ceiling}")

    for service in (
        "safety", "control", "scale", "network", "persistence",
        "diagnostics", "platform",
    ):
        directory = ROOT / "src" / service
        if not directory.is_dir():
            failures.append(f"service directory missing: src/{service}")
        for implementation in directory.glob("*.inc"):
            lines = len(implementation.read_text(encoding="utf-8").splitlines())
            if lines > 1500:
                failures.append(
                    f"service implementation exceeds cap: "
                    f"{implementation.relative_to(ROOT)} {lines}>1500"
                )

    if failures:
        for failure in failures:
            print(f"architecture error: {failure}", file=sys.stderr)
        return 1
    print(
        "architecture boundaries: "
        "Safety/Control/Scale/Machine/Network/Persistence/Diagnostics OK"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
