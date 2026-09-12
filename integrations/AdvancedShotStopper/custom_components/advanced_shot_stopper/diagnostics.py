"""Redacted diagnostics for Advanced Shot Stopper."""

from homeassistant.components.diagnostics import async_redact_data
from homeassistant.core import HomeAssistant

from . import ShotStopperConfigEntry
from .const import CONF_WEBHOOK_ID

TO_REDACT = {"host", CONF_WEBHOOK_ID, "url", "correlationId", "device_id"}


async def async_get_config_entry_diagnostics(
    hass: HomeAssistant, entry: ShotStopperConfigEntry
) -> dict[str, object]:
    """Return useful state without controller or webhook identifiers."""
    runtime = entry.runtime_data
    data = runtime.coordinator.data
    return async_redact_data(
        {
            "entry": dict(entry.data),
            "snapshot": {
                "device_id": data.snapshot.device_id,
                "firmware_version": data.snapshot.firmware_version,
                "capabilities": sorted(data.snapshot.capabilities),
                "shot_state": data.snapshot.shot_state,
                "preset_revision": data.snapshot.preset_revision,
            },
            "preset_count": len(data.presets.items),
            "last_update_success": runtime.coordinator.last_update_success,
        },
        TO_REDACT,
    )
