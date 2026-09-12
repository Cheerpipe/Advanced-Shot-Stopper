"""Coordinator, webhook, entity, storage, and diagnostics tests."""

from __future__ import annotations

import asyncio
import json
from dataclasses import replace
from unittest.mock import AsyncMock, patch

import pytest
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.update_coordinator import UpdateFailed

from custom_components.advanced_shot_stopper.api import CannotConnect, RequestRejected
from custom_components.advanced_shot_stopper.button import RestartButton
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
from custom_components.advanced_shot_stopper.switch import (
    DESCRIPTIONS,
    QuickSettingSwitch,
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
    migrated = Shot.from_dict(fixture("webhook_end_v1.json"))
    coordinator._stored_good = migrated
    result = await coordinator._async_update_data()
    assert result.snapshot.shot_state == "idle"
    assert result.last_good_shot is None
    recovered = Shot.from_dict(fixture("webhook_end_v1.json"))
    api.async_snapshot.return_value = result.snapshot.__class__.from_dict(
        {
            **fixture("integration_snapshot.json"),
            "lastShot": recovered.to_dict(),
        }
    )
    coordinator._stored_good = migrated
    result = await coordinator._async_update_data()
    assert result.last_good_shot is None
    assert coordinator._stored_good is None
    api.async_snapshot.return_value = result.snapshot.__class__.from_dict(
        {
            **fixture("integration_snapshot.json"),
            "lastShot": recovered.to_dict(),
            "lastGoodShot": recovered.to_dict(),
        }
    )
    coordinator._stored_good = None
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


async def test_refresh_requires_coherent_rest_snapshots(hass) -> None:
    """A bounded retry never publishes mixed preset and settings revisions."""
    coordinator, api, _entry = _coordinator(hass)
    mismatched = replace(coordinator.data.presets, revision=20)
    api.async_presets.side_effect = [mismatched, mismatched]
    with pytest.raises(UpdateFailed, match="inconsistent"):
        await coordinator._async_update_data()
    assert api.async_snapshot.await_count == 2
    assert api.async_presets.await_count == 2

    api.async_snapshot.reset_mock()
    api.async_presets.side_effect = [mismatched, coordinator.data.presets]
    result = await coordinator._async_update_data()
    assert result.snapshot.preset_revision == result.presets.revision == 19
    assert api.async_snapshot.await_count == 2


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


async def test_quick_settings_and_controller_started_webhooks(hass) -> None:
    """Complete settings pushes apply immutably and boot hints refresh once."""
    coordinator, _api, _entry = _coordinator(hass)
    quick = _event("webhook_quick_settings_changed_v1.json")
    with patch.object(coordinator, "_schedule_explicit_refresh") as schedule:
        await coordinator.async_process_webhook(quick)
    assert not coordinator.data.snapshot.quick_settings.fast_extraction_guard_enabled
    assert coordinator.data.snapshot.preset_revision == 20
    schedule.assert_not_called()

    gap = _event(
        "webhook_quick_settings_changed_v1.json", uptimeMs=930101, revision=23
    )
    with patch.object(coordinator, "_schedule_explicit_refresh") as schedule:
        await coordinator.async_process_webhook(gap)
    schedule.assert_called_once()

    started = _event("webhook_controller_started_v1.json")
    with patch.object(coordinator, "_schedule_explicit_refresh") as schedule:
        schedule.side_effect = lambda _name, required_boot_id: setattr(
            coordinator, "_required_boot_id", required_boot_id
        )
        await coordinator.async_process_webhook(started)
        await coordinator.async_process_webhook(started)
    schedule.assert_called_once()

    invalid = fixture("webhook_controller_started_v1.json")
    invalid.update(bootId=125, uptimeMs=11, revision=True)
    with pytest.raises(ProtocolError, match="revision"):
        await coordinator.async_process_webhook(
            WebhookEvent.from_bytes(json.dumps(invalid).encode())
        )


async def test_webhooks_reject_other_boots_until_reconciled(hass) -> None:
    """Old and unexpectedly new boot events cannot overwrite the REST snapshot."""
    coordinator, _api, _entry = _coordinator(hass)
    coordinator.async_set_updated_data(
        replace(
            coordinator.data,
            snapshot=replace(coordinator.data.snapshot, boot_id=124),
        )
    )
    original = coordinator.data
    old_events = [
        _event("webhook_end_v1.json"),
        _event("webhook_presets_changed_v1.json"),
        _event("webhook_quick_settings_changed_v1.json"),
    ]
    brew = {
        "schemaVersion": 1,
        "event": "brew_state",
        "deviceId": coordinator.device_id,
        "bootId": 123,
        "cycleId": 50,
        "uptimeMs": 940000,
        "state": "brewing",
    }
    old_events.append(WebhookEvent.from_bytes(json.dumps(brew).encode()))
    with patch.object(coordinator, "_schedule_explicit_refresh") as schedule:
        for event in old_events:
            await coordinator.async_process_webhook(event)
    assert coordinator.data == original
    schedule.assert_not_called()

    brew.update(bootId=125)
    with patch.object(coordinator, "_schedule_explicit_refresh") as schedule:
        await coordinator.async_process_webhook(
            WebhookEvent.from_bytes(json.dumps(brew).encode())
        )
    assert coordinator.data == original
    schedule.assert_called_once_with(
        "advanced_shot_stopper newer-boot reconciliation", required_boot_id=125
    )


async def test_boot_hint_coalesces_with_active_refresh(hass) -> None:
    """A boot hint arriving mid-refresh forces one follow-up reconciliation."""
    coordinator, _api, _entry = _coordinator(hass)
    entered = asyncio.Event()
    release = asyncio.Event()
    calls = 0

    async def refresh() -> None:
        nonlocal calls
        calls += 1
        if calls == 1:
            entered.set()
            await release.wait()
        else:
            coordinator.async_set_updated_data(
                replace(
                    coordinator.data,
                    snapshot=replace(coordinator.data.snapshot, boot_id=124),
                )
            )

    coordinator.async_request_refresh = AsyncMock(side_effect=refresh)
    coordinator._schedule_explicit_refresh("gap")
    await entered.wait()
    await coordinator.async_process_webhook(_event("webhook_controller_started_v1.json"))
    release.set()
    await coordinator._refresh_task
    assert calls == 2
    assert coordinator.data.snapshot.boot_id == 124
    assert coordinator._required_boot_id is None
    assert coordinator.last_update_success


async def test_successful_boot_refresh_cancels_sleeping_recovery(hass) -> None:
    """An independent successful refresh retires backoff before another GET."""
    coordinator, _api, _entry = _coordinator(hass)
    sleeping = asyncio.Event()
    blocked = asyncio.Event()

    async def sleep(_delay: int) -> None:
        sleeping.set()
        await blocked.wait()

    coordinator.async_set_update_error(UpdateFailed("offline"))
    with patch(
        "custom_components.advanced_shot_stopper.coordinator.asyncio.sleep",
        side_effect=sleep,
    ):
        coordinator._start_recovery()
        await sleeping.wait()
        coordinator.async_request_refresh = AsyncMock(
            side_effect=lambda: coordinator.async_set_updated_data(
                replace(
                    coordinator.data,
                    snapshot=replace(coordinator.data.snapshot, boot_id=124),
                )
            )
        )
        await coordinator.async_process_webhook(
            _event("webhook_controller_started_v1.json")
        )
        await coordinator._refresh_task
    await asyncio.sleep(0)
    assert coordinator._recovery_task.cancelled()
    coordinator.async_request_refresh.assert_awaited_once()


async def test_webhook_state_test_and_permission(hass) -> None:
    """State/test pushes update exactly their intended state."""
    coordinator, _api, _entry = _coordinator(hass)
    state = {
        "schemaVersion": 1,
        "event": "brew_state",
        "deviceId": coordinator.device_id,
        "bootId": 123,
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


async def test_switches_and_restart_button(hass) -> None:
    """All actions are serialized, confirmed, and never optimistic."""
    coordinator, api, _entry = _coordinator(hass)
    coordinator.async_confirmed_command = AsyncMock()
    switches = [QuickSettingSwitch(coordinator, item) for item in DESCRIPTIONS]
    assert len(switches) == 7
    assert all(item.available for item in switches)
    assert all(item.is_on for item in switches)
    no_scale = next(
        item for item in switches if item.entity_description.key == "no_scale_bbw"
    )
    await no_scale.async_turn_off()
    command = coordinator.async_confirmed_command.await_args.args[0]
    await command()
    api.async_set_quick_setting.assert_awaited_with("noScaleBbwMode", "off", 19)
    api.async_set_quick_setting.reset_mock()
    await no_scale.async_turn_on()
    command = coordinator.async_confirmed_command.await_args.args[0]
    await command()
    api.async_set_quick_setting.assert_awaited_with(
        "noScaleBbwMode", "warn_once", 19
    )

    brew = switches[0]
    await brew.async_turn_off()
    command = coordinator.async_confirmed_command.await_args.args[0]
    await command()
    api.async_set_quick_setting.assert_awaited_with("brewByWeight", False, 19)

    button = RestartButton(coordinator)
    assert button.unique_id.endswith("_restart")
    await button.async_press()
    assert coordinator.async_confirmed_command.await_args.kwargs == {"restarting": True}

    coordinator.async_confirmed_command.side_effect = RequestRejected("FAILED", 500)
    with pytest.raises(HomeAssistantError):
        await brew.async_turn_on()
    with pytest.raises(HomeAssistantError):
        await button.async_press()


async def test_switch_availability_matches_home_quick_settings(hass) -> None:
    """BBW dependencies and the active-cycle lock match the Web UI Home rules."""
    coordinator, _api, _entry = _coordinator(hass)
    switches = [QuickSettingSwitch(coordinator, item) for item in DESCRIPTIONS]
    quick = replace(coordinator.data.snapshot.quick_settings, brew_by_weight=False)
    coordinator.async_set_updated_data(
        replace(
            coordinator.data,
            snapshot=replace(coordinator.data.snapshot, quick_settings=quick),
        )
    )
    availability = {
        item.entity_description.key: item.available for item in switches
    }
    assert availability == {
        "brew_by_weight": True,
        "no_scale_bbw": False,
        "auto_to_manual_guard": False,
        "slow_extraction_guard": False,
        "fast_extraction_guard": False,
        "avoid_accidental_touch": False,
        "cup_protection": False,
    }
    coordinator.async_set_updated_data(
        replace(
            coordinator.data,
            snapshot=replace(coordinator.data.snapshot, shot_state="brewing"),
        )
    )
    assert not any(item.available for item in switches)
    coordinator.async_set_update_error(UpdateFailed("offline"))
    assert not any(item.available for item in switches)


async def test_real_coordinator_shutdown_stops_future_refreshes(hass) -> None:
    """Unload performs both local task cleanup and coordinator base shutdown."""
    coordinator, api, _entry = _coordinator(hass)
    await coordinator.async_shutdown()
    assert coordinator._shutdown_requested
    await coordinator.async_request_refresh()
    api.async_snapshot.assert_not_awaited()


async def test_command_failure_and_bounded_recovery(hass) -> None:
    """Transport loss starts one finite recovery sequence and retains data."""
    coordinator, api, _entry = _coordinator(hass)
    api.async_set_quick_setting.side_effect = CannotConnect()
    with (
        patch.object(coordinator, "_start_recovery") as recover,
        pytest.raises(CannotConnect),
    ):
        await coordinator.async_confirmed_command(
            lambda: api.async_set_quick_setting("brewByWeight", False, 19)
        )
    assert not coordinator.last_update_success
    recover.assert_called_once()

    coordinator.last_update_success = False
    coordinator.async_refresh = AsyncMock()
    with patch("asyncio.sleep", new=AsyncMock()) as sleep:
        await coordinator._async_recover()
    assert [call.args[0] for call in sleep.await_args_list] == [5, 10, 20, 40, 60]
    assert coordinator.async_refresh.await_count == 5

    coordinator.async_refresh = AsyncMock(
        side_effect=lambda: setattr(coordinator, "last_update_success", True)
    )
    with patch("asyncio.sleep", new=AsyncMock()):
        await coordinator._async_recover()
    coordinator.async_refresh.assert_awaited_once()

    coordinator.async_request_refresh = AsyncMock()
    coordinator.last_update_success = True
    await coordinator.async_confirmed_command(AsyncMock())
    coordinator.async_request_refresh.assert_awaited_once()
    with patch.object(coordinator, "_transport_failed") as failed:
        await coordinator.async_confirmed_command(AsyncMock(), restarting=True)
    failed.assert_called_once()


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
