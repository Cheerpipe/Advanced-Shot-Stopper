"""Protocol contract tests."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from custom_components.advanced_shot_stopper.models import (
    DeviceSnapshot,
    PresetState,
    ProtocolError,
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
    assert snapshot.shot_state == "idle"
    assert presets.active_id == 2
    assert [item.name for item in presets.items] == ["Double", "Single"]


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
        ("apiVersion", 2),
        ("deviceId", "invalid"),
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


@pytest.mark.parametrize("missing", ["webhook_v1", "preset_select_v1"])
def test_snapshot_requires_integration_capabilities(missing: str) -> None:
    payload = load("integration_snapshot.json")
    payload["capabilities"].remove(missing)
    with pytest.raises(ProtocolError, match="capability"):
        DeviceSnapshot.from_dict(payload)


def test_snapshot_accepts_embedded_last_shot() -> None:
    """A REST snapshot may seed the aggregate before any webhook arrives."""
    payload = load("integration_snapshot.json")
    payload["lastShot"] = load("webhook_end_v1.json")
    assert DeviceSnapshot.from_dict(payload).last_shot.preset_name == "Double"


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
