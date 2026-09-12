"""Config-entry setup, unload, removal, and platform tests."""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from homeassistant.exceptions import ConfigEntryNotReady

from custom_components.advanced_shot_stopper import (
    async_remove_entry,
    async_setup_entry,
    async_unload_entry,
)
from custom_components.advanced_shot_stopper.button import (
    async_setup_entry as async_setup_button,
)
from custom_components.advanced_shot_stopper.runtime import ShotStopperRuntimeData
from custom_components.advanced_shot_stopper.select import (
    async_setup_entry as async_setup_select,
)
from custom_components.advanced_shot_stopper.sensor import (
    async_setup_entry as async_setup_sensors,
)
from custom_components.advanced_shot_stopper.switch import (
    async_setup_entry as async_setup_switches,
)

from .helpers import api_mock, config_entry, coordinator_data


def _coordinator_double() -> MagicMock:
    coordinator = MagicMock()
    coordinator.data = coordinator_data()
    coordinator.async_load_store = AsyncMock()
    coordinator.async_config_entry_first_refresh = AsyncMock()
    coordinator.async_shutdown = AsyncMock()
    return coordinator


async def test_setup_order_device_and_unload(hass) -> None:
    """Setup verifies the callback before adding one device and its entities."""
    entry = config_entry()
    entry.add_to_hass(hass)
    api = api_mock()
    coordinator = _coordinator_double()
    events: list[str] = []
    coordinator.async_config_entry_first_refresh.side_effect = lambda: events.append(
        "refresh"
    )
    runtime = MagicMock(spec=ShotStopperRuntimeData)
    runtime.coordinator = coordinator
    runtime.async_register_webhook.side_effect = lambda _value: events.append(
        "register"
    )
    runtime.async_configure_and_test = AsyncMock(
        side_effect=lambda _url: events.append("test")
    )
    with (
        patch(
            "custom_components.advanced_shot_stopper.ShotStopperApi", return_value=api
        ),
        patch(
            "custom_components.advanced_shot_stopper.ShotStopperCoordinator",
            return_value=coordinator,
        ),
        patch(
            "custom_components.advanced_shot_stopper.ShotStopperRuntimeData",
            return_value=runtime,
        ),
        patch(
            "custom_components.advanced_shot_stopper.webhook_url",
            return_value="http://home/webhook",
        ),
        patch.object(
            hass.config_entries, "async_forward_entry_setups", new=AsyncMock()
        ) as forward,
    ):
        assert await async_setup_entry(hass, entry)
    assert events == ["register", "test", "refresh"]
    forward.assert_awaited_once()
    assert entry.runtime_data is runtime

    with patch.object(
        hass.config_entries,
        "async_unload_platforms",
        new=AsyncMock(return_value=True),
    ):
        assert await async_unload_entry(hass, entry)
    runtime.async_unregister_webhook.assert_called_once()
    coordinator.async_shutdown.assert_awaited_once()


async def test_setup_rolls_back_receiver(hass) -> None:
    """Every setup failure unregisters the receiver and restores remote state."""
    entry = config_entry()
    api = api_mock()
    coordinator = _coordinator_double()
    runtime = MagicMock(spec=ShotStopperRuntimeData)
    runtime.async_configure_and_test = AsyncMock(side_effect=RuntimeError("offline"))
    with (
        patch(
            "custom_components.advanced_shot_stopper.ShotStopperApi", return_value=api
        ),
        patch(
            "custom_components.advanced_shot_stopper.ShotStopperCoordinator",
            return_value=coordinator,
        ),
        patch(
            "custom_components.advanced_shot_stopper.ShotStopperRuntimeData",
            return_value=runtime,
        ),
        patch(
            "custom_components.advanced_shot_stopper.webhook_url",
            return_value="http://home/webhook",
        ),
        pytest.raises(ConfigEntryNotReady),
    ):
        await async_setup_entry(hass, entry)
    runtime.async_unregister_webhook.assert_called_once()
    api.async_apply_webhook_config.assert_awaited_once()


async def test_failed_platform_unload_keeps_receiver(hass) -> None:
    """A failed platform unload leaves the active receiver registered."""
    entry = config_entry()
    entry.runtime_data = MagicMock()
    with patch.object(
        hass.config_entries,
        "async_unload_platforms",
        new=AsyncMock(return_value=False),
    ):
        assert not await async_unload_entry(hass, entry)
    entry.runtime_data.async_unregister_webhook.assert_not_called()


async def test_remove_cleans_only_owned_callback(hass) -> None:
    """Removal disables only a callback still owned by this entry."""
    entry = config_entry()
    api = api_mock()
    api.async_webhook_config.return_value["url"] = "http://home/webhook"
    with (
        patch(
            "custom_components.advanced_shot_stopper.ShotStopperApi", return_value=api
        ),
        patch(
            "custom_components.advanced_shot_stopper.webhook_url",
            return_value="http://home/webhook",
        ),
    ):
        await async_remove_entry(hass, entry)
    api.async_configure_webhook.assert_awaited_once_with("", enabled=False)

    api.reset_mock()
    api.async_webhook_config.return_value = {"url": "http://other"}
    with (
        patch(
            "custom_components.advanced_shot_stopper.ShotStopperApi", return_value=api
        ),
        patch(
            "custom_components.advanced_shot_stopper.webhook_url",
            return_value="http://home/webhook",
        ),
    ):
        await async_remove_entry(hass, entry)
    api.async_configure_webhook.assert_not_awaited()


async def test_platform_factories(hass) -> None:
    """Sensor and select platforms create the exact entity contract."""
    entry = config_entry()
    runtime = MagicMock()
    runtime.coordinator.data = coordinator_data()
    runtime.coordinator.api.host = "stopper.local"
    entry.runtime_data = runtime
    add = MagicMock()
    await async_setup_sensors(hass, entry, add)
    assert len(add.call_args.args[0]) == 17
    add.reset_mock()
    await async_setup_select(hass, entry, add)
    assert len(add.call_args.args[0]) == 1
    add.reset_mock()
    await async_setup_switches(hass, entry, add)
    assert len(list(add.call_args.args[0])) == 7
    add.reset_mock()
    await async_setup_button(hass, entry, add)
    assert len(add.call_args.args[0]) == 1
