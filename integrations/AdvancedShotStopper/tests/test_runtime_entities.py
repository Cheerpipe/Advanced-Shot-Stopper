"""Coordinator, webhook, entity, storage, and diagnostics tests."""

from __future__ import annotations

import json
from unittest.mock import AsyncMock, patch

import pytest
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.update_coordinator import UpdateFailed

from custom_components.advanced_shot_stopper.api import RequestRejected
from custom_components.advanced_shot_stopper.const import STOP_DETAILS
from custom_components.advanced_shot_stopper.coordinator import ShotStopperCoordinator
from custom_components.advanced_shot_stopper.diagnostics import (
    async_get_config_entry_diagnostics,
)
from custom_components.advanced_shot_stopper.models import (
    ProtocolError,
    Shot,
    WebhookEvent,
)
from custom_components.advanced_shot_stopper.repairs import async_create_webhook_repair
from custom_components.advanced_shot_stopper.runtime import ShotStopperRuntimeData
from custom_components.advanced_shot_stopper.select import ActivePresetSelect
from custom_components.advanced_shot_stopper.sensor import (
    SHOT_DESCRIPTIONS,
    ShotStateSensor,
    StoredShotSensor,
)

from .helpers import (
    FIXTURES,
    WEBHOOK_ID,
    api_mock,
    config_entry,
    coordinator_data,
    fixture,
)


def _coordinator(hass):
    entry = config_entry()
    api = api_mock()
    coordinator = ShotStopperCoordinator(hass, entry, entry.data["device_id"], api)
    coordinator.async_set_updated_data(coordinator_data())
    return coordinator, api, entry


def _event(name: str, **updates) -> WebhookEvent:
    payload = fixture(name)
    payload.update(updates)
    return WebhookEvent.from_bytes(json.dumps(payload).encode())


async def test_coordinator_refresh_and_failures(hass) -> None:
    """REST reconciliation publishes data and maps availability failures."""
    coordinator, api, _entry = _coordinator(hass)
    result = await coordinator._async_update_data()
    assert result.snapshot.shot_state == "idle"
    recovered = Shot.from_dict(fixture("webhook_end_v1.json"))
    api.async_snapshot.return_value = result.snapshot.__class__.from_dict(
        {**fixture("integration_snapshot.json"), "lastShot": recovered.to_dict()}
    )
    with patch.object(coordinator._store, "async_delay_save") as save:
        result = await coordinator._async_update_data()
    assert result.last_shot == recovered
    assert result.last_good_shot == recovered
    save.assert_called_once()
    api.async_snapshot.side_effect = RuntimeError("offline")
    with pytest.raises(UpdateFailed, match="offline"):
        await coordinator._async_update_data()
    api.async_snapshot.side_effect = None
    api.async_snapshot.return_value = coordinator_data().snapshot.__class__.from_dict(
        {**fixture("integration_snapshot.json"), "deviceId": "11:22:33:44:55:66"}
    )
    with pytest.raises(UpdateFailed, match="identity"):
        await coordinator._async_update_data()


async def test_store_restore_save_and_corruption(hass) -> None:
    """The compact aggregate survives restart and ignores corrupt records."""
    coordinator, _api, _entry = _coordinator(hass)
    shot = Shot.from_dict(fixture("webhook_end_v1.json"))
    coordinator._store.async_load = AsyncMock(
        return_value={"last_shot": shot.to_dict(), "last_good_shot": shot.to_dict()}
    )
    await coordinator.async_load_store()
    assert coordinator._stored_last == shot
    assert coordinator._storage_data()["last_good_shot"]["presetName"] == "Double"
    coordinator._store.async_load.return_value = {"last_shot": {"bad": True}}
    await coordinator.async_load_store()
    assert coordinator._stored_last is None
    coordinator._store.async_load.return_value = []
    await coordinator.async_load_store()


async def test_webhook_ordering_aggregates_and_gap_refresh(hass) -> None:
    """Pushes deduplicate, reject stale data, and refresh revision gaps."""
    coordinator, _api, _entry = _coordinator(hass)
    end = _event("webhook_end_v1.json")
    with patch.object(coordinator._store, "async_delay_save") as save:
        await coordinator.async_process_webhook(end)
        await coordinator.async_process_webhook(end)
    assert coordinator.data.last_shot.preset_name == "Double"
    assert coordinator.data.last_good_shot == coordinator.data.last_shot
    save.assert_called_once()

    short_payload = fixture("webhook_end_v1.json")
    short_payload.update(cycleId=8, uptimeMs=930000, durationMs=10000, weightG=1.5)
    await coordinator.async_process_webhook(
        WebhookEvent.from_bytes(json.dumps(short_payload).encode())
    )
    assert coordinator.data.last_good_shot.cycle_id == end.cycle_id

    stale = _event("webhook_end_v1.json", cycleId=9, uptimeMs=1)
    await coordinator.async_process_webhook(stale)
    assert coordinator.data.last_shot.cycle_id == 8

    changed = _event("webhook_presets_changed_v1.json")
    changed.data["revision"] = coordinator.data.presets.revision + 3
    with patch.object(coordinator, "async_request_refresh", new=AsyncMock()) as refresh:
        await coordinator.async_process_webhook(changed)
        await hass.async_block_till_done()
    refresh.assert_awaited_once()
    assert coordinator.data.snapshot.active_preset_id == changed.data["activeId"]
    accepted_revision = coordinator.data.presets.revision
    lower = _event(
        "webhook_presets_changed_v1.json", uptimeMs=changed.uptime_ms + 1
    )
    lower.data["revision"] = accepted_revision - 1
    lower.data["activeId"] = 1
    await coordinator.async_process_webhook(lower)
    assert coordinator.data.presets.revision == accepted_revision


async def test_webhook_state_test_and_permission(hass) -> None:
    """State/test pushes update exactly their intended state."""
    coordinator, _api, _entry = _coordinator(hass)
    state = {
        "schemaVersion": 1,
        "event": "brew_state",
        "deviceId": coordinator.device_id,
        "bootId": 124,
        "cycleId": 10,
        "uptimeMs": 10,
        "state": "brewing",
    }
    await coordinator.async_process_webhook(
        WebhookEvent.from_bytes(json.dumps(state).encode())
    )
    assert coordinator.data.snapshot.shot_state == "brewing"
    state.update(uptimeMs=11, state="broken")
    with pytest.raises(ProtocolError):
        await coordinator.async_process_webhook(
            WebhookEvent.from_bytes(json.dumps(state).encode())
        )
    state["state"] = "idle"
    await coordinator.async_process_webhook(
        WebhookEvent.from_bytes(json.dumps(state).encode())
    )
    assert coordinator.data.snapshot.shot_state == "idle"

    waiter = coordinator.expect_test("proof")
    state.update(event="test", uptimeMs=12, correlationId="proof")
    await coordinator.async_process_webhook(
        WebhookEvent.from_bytes(json.dumps(state).encode())
    )
    assert waiter.done()
    coordinator.cancel_test("absent")
    pending = coordinator.expect_test("cancel")
    coordinator.cancel_test("cancel")
    assert pending.cancelled()

    state.update(deviceId="11:22:33:44:55:66", uptimeMs=13)
    with pytest.raises(PermissionError):
        await coordinator.async_process_webhook(
            WebhookEvent.from_bytes(json.dumps(state).encode())
        )


class _Request:
    def __init__(self, payload: bytes, content_type: str = "application/json") -> None:
        self.payload = payload
        self.content_type = content_type
        self.content_length = len(payload)
        self.content = self

    async def read(self, maximum: int = -1) -> bytes:
        return self.payload if maximum < 0 else self.payload[:maximum]


async def test_runtime_register_validate_and_transaction(hass) -> None:
    """The receiver is local POST-only and returns deterministic statuses."""
    coordinator, api, _entry = _coordinator(hass)
    runtime = ShotStopperRuntimeData(
        hass, api, coordinator, WEBHOOK_ID, "http://home/webhook"
    )
    with patch(
        "custom_components.advanced_shot_stopper.runtime.webhook.async_register"
    ) as register:
        runtime.async_register_webhook(WEBHOOK_ID)
        runtime.async_register_webhook(WEBHOOK_ID)
    register.assert_called_once()
    assert register.call_args.kwargs == {
        "local_only": True,
        "allowed_methods": ["POST"],
    }

    good = (FIXTURES / "webhook_end_v1.json").read_bytes()
    assert (
        await runtime.async_handle_webhook(hass, WEBHOOK_ID, _Request(good))
    ).status == 204
    assert (
        await runtime.async_handle_webhook(
            hass, WEBHOOK_ID, _Request(good, "text/plain")
        )
    ).status == 400
    oversized = _Request(b"{}")
    oversized.content_length = 9000
    assert (
        await runtime.async_handle_webhook(hass, WEBHOOK_ID, oversized)
    ).status == 400
    chunked = _Request(b"x" * 9000)
    chunked.content_length = None
    assert (
        await runtime.async_handle_webhook(hass, WEBHOOK_ID, chunked)
    ).status == 400
    assert (
        await runtime.async_handle_webhook(hass, WEBHOOK_ID, _Request(b"bad"))
    ).status == 400
    wrong = fixture("webhook_end_v1.json")
    wrong["deviceId"] = "11:22:33:44:55:66"
    assert (
        await runtime.async_handle_webhook(
            hass, WEBHOOK_ID, _Request(json.dumps(wrong).encode())
        )
    ).status == 403

    with (
        patch(
            "custom_components.advanced_shot_stopper.runtime.secrets.token_urlsafe",
            return_value="proof",
        ),
        patch.object(coordinator, "expect_test") as expect,
    ):
        future = hass.loop.create_future()
        future.set_result(None)
        expect.return_value = future
        await runtime.async_configure_and_test("http://home/new")
    api.async_configure_webhook.assert_awaited_with("http://home/new")
    api.async_test_webhook.assert_awaited_with("proof")

    with patch(
        "custom_components.advanced_shot_stopper.runtime.webhook.async_unregister"
    ) as unregister:
        runtime.async_unregister_webhook()
        runtime.async_unregister_webhook("other")
    assert unregister.call_count == 2


async def test_entities_and_select(hass) -> None:
    """Entities expose stable IDs/metadata and the select is non-optimistic."""
    coordinator, api, _entry = _coordinator(hass)
    state = ShotStateSensor(coordinator)
    assert state.unique_id.endswith("_shot_state")
    assert state.native_value == "idle"
    assert state.device_info["identifiers"]
    assert state.device_info["name"] == "Advanced Shot Stopper"
    assert len(SHOT_DESCRIPTIONS) == 16
    empty = [StoredShotSensor(coordinator, item) for item in SHOT_DESCRIPTIONS]
    assert all(entity.native_value is None for entity in empty)
    await coordinator.async_process_webhook(_event("webhook_end_v1.json"))
    entities = [StoredShotSensor(coordinator, item) for item in SHOT_DESCRIPTIONS]
    values = {entity.entity_description.key: entity.native_value for entity in entities}
    assert values["last_shot_duration"] == 27.8
    assert values["last_shot_final_weight"] == 36.72
    assert next(
        entity for entity in entities if entity.entity_description.key.endswith("_type")
    ).options == ["auto", "timer_only", "manual"]
    assert next(
        entity
        for entity in entities
        if entity.entity_description.key.endswith("_stop_detail")
    ).options == list(STOP_DETAILS)

    select = ActivePresetSelect(coordinator)
    assert select.options == ["Double", "Single"]
    assert select.current_option == "Double"
    with patch.object(coordinator, "async_request_refresh", new=AsyncMock()) as refresh:
        await select.async_select_option("Double")
    api.async_select_preset.assert_awaited_once_with(2)
    refresh.assert_awaited_once()
    with pytest.raises(HomeAssistantError):
        await select.async_select_option("missing")
    api.async_select_preset.side_effect = RequestRejected("FAILED", 500)
    with pytest.raises(HomeAssistantError):
        await select.async_select_option("Double")
    for failure in (TimeoutError(), ProtocolError("bad confirmation")):
        api.async_select_preset.side_effect = failure
        with pytest.raises(HomeAssistantError):
            await select.async_select_option("Double")


async def test_diagnostics_redact_identifiers(hass) -> None:
    """Diagnostics retain useful state but redact all secrets and addresses."""
    coordinator, api, entry = _coordinator(hass)
    runtime = ShotStopperRuntimeData(
        hass, api, coordinator, WEBHOOK_ID, "http://home/webhook"
    )
    entry.runtime_data = runtime
    result = await async_get_config_entry_diagnostics(hass, entry)
    rendered = json.dumps(result)
    assert WEBHOOK_ID not in rendered
    assert "stopper.local" not in rendered
    assert "AA:BB:CC:DD:EE:FF" not in rendered
    async_create_webhook_repair(hass, entry.entry_id)
