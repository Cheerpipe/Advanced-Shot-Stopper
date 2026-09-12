"""Strict protocol models for Advanced Shot Stopper."""

from __future__ import annotations

import json
import math
import re
from dataclasses import asdict, dataclass
from typing import Any, Self

from .const import API_VERSION, REQUIRED_CAPABILITIES, SHOT_TYPES, STOP_DETAILS

MAX_WEBHOOK_BYTES = 8192
MAX_PRESETS = 8
DEVICE_ID = re.compile(r"^[0-9A-F]{2}(?::[0-9A-F]{2}){5}$")
EVENT_TYPES = {"brew_state", "first_drop", "end", "test", "presets_changed"}


class ProtocolError(ValueError):
    """The controller returned data outside the public contract."""


def _mapping(value: Any, field: str = "response") -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ProtocolError(f"{field} must be an object")
    return value


def _string(value: Any, field: str, maximum: int) -> str:
    if not isinstance(value, str) or not value or len(value) > maximum:
        raise ProtocolError(f"{field} is invalid")
    return value


def _integer(value: Any, field: str, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ProtocolError(f"{field} is invalid")
    return value


def _number(value: Any, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ProtocolError(f"{field} is invalid")
    result = float(value)
    if not math.isfinite(result) or result < 0:
        raise ProtocolError(f"{field} is invalid")
    return result


@dataclass(frozen=True, slots=True)
class Preset:
    """A named controller preset."""

    id: int
    name: str
    is_factory: bool


@dataclass(frozen=True, slots=True)
class PresetState:
    """Authoritative preset inventory."""

    active_id: int
    revision: int
    items: tuple[Preset, ...]

    @classmethod
    def from_dict(cls, value: Any) -> Self:
        data = _mapping(value)
        if data.get("apiVersion") != API_VERSION:
            raise ProtocolError("unsupported preset API version")
        raw_items = data.get("items")
        if not isinstance(raw_items, list) or len(raw_items) > MAX_PRESETS:
            raise ProtocolError("items is invalid")
        items: list[Preset] = []
        ids: set[int] = set()
        names: set[str] = set()
        for raw in raw_items:
            item = _mapping(raw, "preset")
            preset_id = _integer(item.get("id"), "preset.id", 1)
            name = _string(item.get("name"), "preset.name", 23)
            factory = item.get("isFactory")
            if not isinstance(factory, bool) or preset_id in ids or name in names:
                raise ProtocolError("preset inventory is ambiguous")
            ids.add(preset_id)
            names.add(name)
            items.append(Preset(preset_id, name, factory))
        active_id = _integer(data.get("activeId"), "activeId", 1)
        if active_id not in ids:
            raise ProtocolError("activeId is missing from items")
        return cls(active_id, _integer(data.get("revision"), "revision"), tuple(items))


@dataclass(frozen=True, slots=True)
class Shot:
    """A completed shot captured by the controller."""

    cycle_id: int
    uptime_ms: int
    duration_ms: int
    target_weight_g: float
    preset_id: int
    preset_name: str
    shot_type: str
    stop_detail: str
    first_drop_ms: int | None = None
    weight_g: float | None = None
    average_flow_gps: float | None = None

    @classmethod
    def from_dict(cls, value: Any) -> Self:
        data = _mapping(value, "shot")
        shot_type = _string(data.get("shotType"), "shotType", 16)
        if shot_type not in SHOT_TYPES:
            raise ProtocolError("shotType is invalid")
        detail = _string(data.get("stopDetail"), "stopDetail", 31)
        if detail == "prediction":
            detail = "other"
        if detail not in STOP_DETAILS:
            raise ProtocolError("stopDetail is invalid")
        optional_int = data.get("firstDropMs")
        optional_weight = data.get("weightG")
        optional_flow = data.get("averageFlowGps")
        return cls(
            cycle_id=_integer(data.get("cycleId"), "cycleId"),
            uptime_ms=_integer(data.get("uptimeMs"), "uptimeMs"),
            duration_ms=_integer(data.get("durationMs"), "durationMs"),
            target_weight_g=_number(data.get("targetWeightG"), "targetWeightG"),
            preset_id=_integer(data.get("presetId"), "presetId"),
            preset_name=_string(data.get("presetName"), "presetName", 23),
            shot_type=shot_type,
            stop_detail=detail,
            first_drop_ms=None
            if optional_int is None
            else _integer(optional_int, "firstDropMs"),
            weight_g=None
            if optional_weight is None
            else _number(optional_weight, "weightG"),
            average_flow_gps=None
            if optional_flow is None
            else _number(optional_flow, "averageFlowGps"),
        )

    def to_dict(self) -> dict[str, Any]:
        """Return a JSON-safe storage representation."""
        data = asdict(self)
        return {
            "cycleId": data["cycle_id"],
            "uptimeMs": data["uptime_ms"],
            "durationMs": data["duration_ms"],
            "targetWeightG": data["target_weight_g"],
            "presetId": data["preset_id"],
            "presetName": data["preset_name"],
            "shotType": data["shot_type"],
            "stopDetail": data["stop_detail"],
            "firstDropMs": data["first_drop_ms"],
            "weightG": data["weight_g"],
            "averageFlowGps": data["average_flow_gps"],
        }


@dataclass(frozen=True, slots=True)
class DeviceSnapshot:
    """Authoritative controller snapshot."""

    device_id: str
    manufacturer: str
    model: str
    firmware_version: str
    capabilities: frozenset[str]
    shot_state: str
    boot_id: int
    active_preset_id: int
    preset_revision: int
    last_shot: Shot | None

    @classmethod
    def from_dict(cls, value: Any) -> Self:
        data = _mapping(value)
        if (
            data.get("apiVersion") != API_VERSION
            or data.get("minimumClientApiVersion", 1) > API_VERSION
        ):
            raise ProtocolError("incompatible API version")
        device_id = _string(data.get("deviceId"), "deviceId", 17).upper()
        if not DEVICE_ID.fullmatch(device_id):
            raise ProtocolError("deviceId is invalid")
        capabilities = data.get("capabilities")
        if not isinstance(capabilities, list) or not all(
            isinstance(item, str) and 0 < len(item) <= 32 for item in capabilities
        ):
            raise ProtocolError("capabilities is invalid")
        if not REQUIRED_CAPABILITIES.issubset(capabilities):
            raise ProtocolError("required capability is missing")
        state = data.get("shotState")
        if state not in ("idle", "brewing"):
            raise ProtocolError("shotState is invalid")
        raw_shot = data.get("lastShot")
        return cls(
            device_id=device_id,
            manufacturer=_string(data.get("manufacturer"), "manufacturer", 64),
            model=_string(data.get("model"), "model", 64),
            firmware_version=_string(
                data.get("firmwareVersion"), "firmwareVersion", 31
            ),
            capabilities=frozenset(capabilities),
            shot_state=state,
            boot_id=_integer(data.get("bootId"), "bootId"),
            active_preset_id=_integer(data.get("activePresetId"), "activePresetId", 1),
            preset_revision=_integer(data.get("presetRevision"), "presetRevision"),
            last_shot=None if raw_shot is None else Shot.from_dict(raw_shot),
        )


@dataclass(frozen=True, slots=True)
class WebhookEvent:
    """Validated webhook envelope."""

    event: str
    device_id: str
    boot_id: int
    cycle_id: int
    uptime_ms: int
    data: dict[str, Any]

    @property
    def deduplication_key(self) -> tuple[str, int, int, str, int]:
        return (self.device_id, self.boot_id, self.cycle_id, self.event, self.uptime_ms)

    @classmethod
    def from_bytes(cls, payload: bytes) -> Self:
        if not payload or len(payload) > MAX_WEBHOOK_BYTES:
            raise ProtocolError("webhook body size is invalid")
        try:
            data = _mapping(json.loads(payload), "webhook")
        except (UnicodeDecodeError, json.JSONDecodeError) as err:
            raise ProtocolError("webhook is not valid JSON") from err
        if data.get("schemaVersion") != 1:
            raise ProtocolError("unsupported webhook schema")
        event = data.get("event")
        if event not in EVENT_TYPES:
            raise ProtocolError("webhook event is invalid")
        device_id = _string(data.get("deviceId"), "deviceId", 17).upper()
        if not DEVICE_ID.fullmatch(device_id):
            raise ProtocolError("deviceId is invalid")
        return cls(
            event=event,
            device_id=device_id,
            boot_id=_integer(data.get("bootId"), "bootId"),
            cycle_id=_integer(data.get("cycleId"), "cycleId"),
            uptime_ms=_integer(data.get("uptimeMs"), "uptimeMs"),
            data=data,
        )
