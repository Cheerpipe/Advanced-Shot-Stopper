"""Protocol contract tests."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from custom_components.open_brew_by_weight.models import (
    DEFAULT_ARCH,
    DEFAULT_HARDWARE_PROFILE,
    DEFAULT_MACHINE_NAME,
    DEFAULT_MACHINE_PROFILE,
    DEFAULT_MANUFACTURER,
    DEFAULT_MODEL,
    DeviceSnapshot,
    PresetState,
    ProtocolError,
    QuickSettings,
    Shot,
    WebhookEvent,
)

FIXTURES = Path(__file__).parents[3] / "src" / "tests" / "fixtures"


def load(name: str) -> dict:
    """Load the firmware-owned cross-side fixture."""
    return json.loads((FIXTURES / name).read_text())


def test_snapshot_and_presets_contract() -> None:
    """Parse identity and unique preset inventory."""
    snapshot = DeviceSnapshot.from_dict(load("integration_snapshot.json"))
    presets = PresetState.from_dict(load("integration_presets.json"))
    assert snapshot.device_id == "AA:BB:CC:DD:EE:FF"
    assert snapshot.manufacturer == "Cheerpipe"
    assert snapshot.model == "Open Brew by Weight"
    assert snapshot.hardware_profile == "esp32-s3-relay-x1-speaker"
    assert snapshot.arch == "n16r8"
    assert snapshot.machine_name == "La Marzocco Linea Micra"
    assert snapshot.machine_profile == "la-marzocco-linea-micra"
    assert snapshot.mdns_host == "controller"
    assert snapshot.wifi_mac == "AA:BB:CC:DD:EE:FF"
    assert snapshot.bluetooth_mac == "AA:BB:CC:DD:EE:10"
    assert snapshot.shot_state == "idle"
    assert snapshot.last_shot is not None and snapshot.last_shot.cycle_id == 42
    assert snapshot.quick_settings.no_scale_bbw_mode == "warn_once"
    assert presets.active_id == 2
    assert [item.name for item in presets.items] == ["Double", "Single"]


def test_ip_changed_webhook_contract() -> None:
    """Parse the IP-change webhook envelope and its address."""
    event = WebhookEvent.from_bytes((FIXTURES / "webhook_ip_changed_v1.json").read_bytes())
    assert event.event == "ip_changed"
    assert event.data["ip"] == "192.168.1.42"


def test_snapshot_identity_fields_fall_back_when_absent() -> None:
    """Older firmware without identity fields still parses with defaults."""
    payload = load("integration_snapshot.json")
    for key in (
        "manufacturer",
        "model",
        "hardwareProfile",
        "arch",
        "machineName",
        "machineProfile",
        "mdnsHost",
    ):
        payload.pop(key)
    snapshot = DeviceSnapshot.from_dict(payload)
    assert snapshot.manufacturer == DEFAULT_MANUFACTURER
    assert snapshot.model == DEFAULT_MODEL
    assert snapshot.hardware_profile == DEFAULT_HARDWARE_PROFILE
    assert snapshot.arch == DEFAULT_ARCH
    assert snapshot.machine_name == DEFAULT_MACHINE_NAME
    assert snapshot.machine_profile == DEFAULT_MACHINE_PROFILE
    assert snapshot.mdns_host is None


def test_shot_and_webhook_contract() -> None:
    """Parse end and preset-change webhooks."""
    end_payload = (FIXTURES / "webhook_end_v1.json").read_bytes()
    event = WebhookEvent.from_bytes(end_payload)
    shot = Shot.from_dict(event.data)
    assert shot.preset_name == "Double"
    assert shot.weight_g == 36.72
    presets = WebhookEvent.from_bytes(
        (FIXTURES / "webhook_presets_changed_v1.json").read_bytes()
    )
    assert presets.event == "presets_changed"
    quick = WebhookEvent.from_bytes(
        (FIXTURES / "webhook_quick_settings_changed_v1.json").read_bytes()
    )
    assert quick.event == "quick_settings_changed"
    started = WebhookEvent.from_bytes(
        (FIXTURES / "webhook_controller_started_v1.json").read_bytes()
    )
    assert started.event == "controller_started"


@pytest.mark.parametrize(
    "payload",
    [
        b"",
        b"not-json",
        b'{"schemaVersion":2}',
        b'{"schemaVersion":1,"event":"unknown","deviceId":"AA:BB:CC:DD:EE:FF","bootId":1,"cycleId":1,"uptimeMs":1}',
        b"{" + b"x" * 8192 + b"}",
    ],
)
def test_rejects_invalid_webhooks(payload: bytes) -> None:
    """Reject malformed, oversized, or incompatible pushes."""
    with pytest.raises(ProtocolError):
        WebhookEvent.from_bytes(payload)


def test_rejects_ambiguous_presets_and_invalid_shot() -> None:
    """Names and IDs must map one-to-one and metrics must be finite."""
    value = load("integration_presets.json")
    value["items"][1]["name"] = value["items"][0]["name"]
    with pytest.raises(ProtocolError):
        PresetState.from_dict(value)
    shot = load("webhook_end_v1.json")
    shot["weightG"] = float("nan")
    with pytest.raises(ProtocolError):
        Shot.from_dict(shot)


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("apiVersion", 2),
        ("items", "not-a-list"),
        ("activeId", 99),
    ],
)
def test_rejects_invalid_preset_envelopes(field: str, value: object) -> None:
    """Preset envelopes enforce version, list shape, and active membership."""
    payload = load("integration_presets.json")
    payload[field] = value
    with pytest.raises(ProtocolError):
        PresetState.from_dict(payload)


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("shotType", "rinse"),
        ("stopDetail", "invented"),
        ("cycleId", True),
        ("presetName", ""),
    ],
)
def test_rejects_invalid_shot_fields(field: str, value: object) -> None:
    """Shot enums, identifiers, and strings stay within the public contract."""
    payload = load("webhook_end_v1.json")
    payload[field] = value
    with pytest.raises(ProtocolError):
        Shot.from_dict(payload)


def test_optional_shot_fields_and_legacy_mapping() -> None:
    """Missing metrics remain unknown and the legacy prediction detail is mapped."""
    payload = load("webhook_end_v1.json")
    payload.update(
        stopDetail="prediction",
        firstDropMs=None,
        weightG=None,
        averageFlowGps=None,
    )
    shot = Shot.from_dict(payload)
    assert shot.stop_detail == "other"
    assert shot.first_drop_ms is None


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("apiVersion", 3),
        ("deviceId", "invalid"),
        ("wifiMac", "aa:bb:cc:dd:ee:ff"),
        ("bluetoothMac", "AA:BB:CC:DD:EE"),
        ("capabilities", ["x" * 33]),
        ("shotState", "unknown"),
    ],
)
def test_rejects_invalid_snapshot_fields(field: str, value: object) -> None:
    """Identity, capabilities, and controller state are strictly bounded."""
    payload = load("integration_snapshot.json")
    payload[field] = value
    with pytest.raises(ProtocolError):
        DeviceSnapshot.from_dict(payload)


@pytest.mark.parametrize(
    "missing",
    [
        "webhook_v1",
        "preset_select_v1",
        "quick_settings_v1",
        "restart_v1",
        "stored_shots_v1",
    ],
)
def test_snapshot_requires_integration_capabilities(missing: str) -> None:
    payload = load("integration_snapshot.json")
    payload["capabilities"].remove(missing)
    with pytest.raises(ProtocolError, match="capability"):
        DeviceSnapshot.from_dict(payload)


def test_snapshot_accepts_embedded_last_shot() -> None:
    """An older REST snapshot still seeds its recorded-shot sensors."""
    payload = load("integration_snapshot.json")
    payload["lastGoodShot"] = load("webhook_end_v1.json")
    payload["lastActivation"] = {
        "id": 1042,
        "type": "rinse",
        "durationS": 12.0,
        "hasWallTime": True,
        "endedAtUnixSec": 1767225611,
        "endedAtLocalSec": 1767236411,
    }
    snapshot = DeviceSnapshot.from_dict(payload)
    assert snapshot.last_shot.preset_name == "Double"
    assert snapshot.last_activation is not None
    assert snapshot.last_activation.type == "rinse"
    assert snapshot.stats is not None
    assert snapshot.stats.avg_flow_gps == 1.55

    payload["lastGoodShot"] = None
    assert DeviceSnapshot.from_dict(payload).last_shot is None


def test_snapshot_reads_unified_recorded_shot() -> None:
    payload = load("integration_snapshot.json")
    payload["apiVersion"] = 2
    payload["minimumClientApiVersion"] = 2
    payload["lastShot"] = payload.pop("lastGoodShot")
    snapshot = DeviceSnapshot.from_dict(payload)
    assert snapshot.last_shot is not None
    assert snapshot.last_shot.duration_ms == 27800
    payload["lastShot"] = None
    assert DeviceSnapshot.from_dict(payload).last_shot is None


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("noScaleBbwMode", "invalid"),
        ("brewByWeight", 1),
        ("activePresetId", 0),
    ],
)
def test_rejects_invalid_quick_settings(field: str, value: object) -> None:
    quick = load("integration_snapshot.json")["quickSettings"]
    quick[field] = value
    with pytest.raises(ProtocolError):
        QuickSettings.from_dict(quick)


def test_rejects_missing_extra_or_inconsistent_quick_settings() -> None:
    quick = load("integration_snapshot.json")["quickSettings"]
    quick["extra"] = True
    with pytest.raises(ProtocolError, match="fields"):
        QuickSettings.from_dict(quick)
    payload = load("integration_snapshot.json")
    payload["quickSettings"]["revision"] += 1
    with pytest.raises(ProtocolError, match="inconsistent"):
        DeviceSnapshot.from_dict(payload)


def test_rejects_wrong_webhook_device() -> None:
    """The envelope validates device identity syntax before dispatch."""
    payload = load("webhook_end_v1.json")
    payload["deviceId"] = "wrong"
    with pytest.raises(ProtocolError):
        WebhookEvent.from_bytes(json.dumps(payload).encode())


def test_rejects_non_object_protocol_values() -> None:
    """Top-level arrays and invalid numeric types cannot enter state."""
    with pytest.raises(ProtocolError, match="object"):
        PresetState.from_dict([])
    payload = load("webhook_end_v1.json")
    payload["weightG"] = "36"
    with pytest.raises(ProtocolError):
        Shot.from_dict(payload)
