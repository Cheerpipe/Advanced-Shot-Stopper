"""Advanced Shot Stopper integration."""

from __future__ import annotations

import contextlib

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryNotReady
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import ApiError, ShotStopperApi
from .const import (
    CONF_DEVICE_ID,
    CONF_WEBHOOK_ID,
    DOMAIN,
    PLATFORMS,
)
from .coordinator import ShotStopperCoordinator
from .runtime import ShotStopperRuntimeData, webhook_url

type ShotStopperConfigEntry = ConfigEntry[ShotStopperRuntimeData]


async def async_setup_entry(hass: HomeAssistant, entry: ShotStopperConfigEntry) -> bool:
    """Register the receiver before configuring the controller."""
    api = ShotStopperApi(async_get_clientsession(hass), entry.data[CONF_HOST])
    coordinator = ShotStopperCoordinator(hass, entry, entry.data[CONF_DEVICE_ID], api)
    await coordinator.async_load_store()
    webhook_id = entry.data[CONF_WEBHOOK_ID]
    callback_url = webhook_url(hass, webhook_id)
    runtime = ShotStopperRuntimeData(hass, api, coordinator, webhook_id, callback_url)
    previous: dict[str, object] | None = None
    runtime.async_register_webhook(webhook_id)
    try:
        previous = await api.async_webhook_config()
        await runtime.async_configure_and_test(callback_url)
        await coordinator.async_config_entry_first_refresh()
    except Exception as err:
        runtime.async_unregister_webhook()
        if previous is not None:
            with contextlib.suppress(ApiError):
                await api.async_apply_webhook_config(previous)
        raise ConfigEntryNotReady(str(err)) from err

    entry.runtime_data = runtime
    registry = dr.async_get(hass)
    snapshot = coordinator.data.snapshot
    registry.async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={(DOMAIN, snapshot.device_id)},
        name="Advanced Shot Stopper",
        manufacturer=snapshot.manufacturer,
        model=snapshot.model,
        sw_version=snapshot.firmware_version,
        configuration_url=f"http://{api.host}/",
    )
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(
    hass: HomeAssistant, entry: ShotStopperConfigEntry
) -> bool:
    """Unload entities and remove only the local receiver."""
    if not await hass.config_entries.async_unload_platforms(entry, PLATFORMS):
        return False
    await entry.runtime_data.coordinator.async_shutdown()
    entry.runtime_data.async_unregister_webhook()
    return True


async def async_remove_entry(
    hass: HomeAssistant, entry: ShotStopperConfigEntry
) -> None:
    """Best-effort conditional callback cleanup."""
    api = ShotStopperApi(async_get_clientsession(hass), entry.data[CONF_HOST])
    owned_url = webhook_url(hass, entry.data[CONF_WEBHOOK_ID])
    current: dict[str, object] | None = None
    with contextlib.suppress(ApiError):
        current = await api.async_webhook_config()
    if current is not None and current.get("url") == owned_url:
        with contextlib.suppress(ApiError):
            await api.async_configure_webhook("", enabled=False)
