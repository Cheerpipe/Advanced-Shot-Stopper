"""Preset selector for Advanced Shot Stopper."""

from homeassistant.components.select import SelectEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import ApiError
from .const import DOMAIN
from .coordinator import ShotStopperCoordinator
from .entity import ShotStopperEntity
from .models import ProtocolError
from .runtime import ShotStopperRuntimeData

PARALLEL_UPDATES = 1


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry[ShotStopperRuntimeData],
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Create the non-optimistic preset selector."""
    runtime: ShotStopperRuntimeData = entry.runtime_data
    async_add_entities([ActivePresetSelect(runtime.coordinator)])


class ActivePresetSelect(ShotStopperEntity, SelectEntity):
    """Select a preset only after durable controller confirmation."""

    _attr_translation_key = "active_preset"
    _attr_icon = "mdi:tune"

    def __init__(self, coordinator: ShotStopperCoordinator) -> None:
        super().__init__(coordinator, "active_preset")

    @property
    def options(self) -> list[str]:
        return [item.name for item in self.coordinator.data.presets.items]

    @property
    def current_option(self) -> str | None:
        state = self.coordinator.data.presets
        return next(
            (item.name for item in state.items if item.id == state.active_id), None
        )

    async def async_select_option(self, option: str) -> None:
        state = self.coordinator.data.presets
        preset = next((item for item in state.items if item.name == option), None)
        if preset is None:
            raise HomeAssistantError(
                translation_domain=DOMAIN, translation_key="preset_not_found"
            )
        async with self.coordinator.command_lock:
            try:
                await self.coordinator.api.async_select_preset(preset.id)
                await self.coordinator.async_request_refresh()
            except (ApiError, ProtocolError, TimeoutError) as err:
                raise HomeAssistantError(
                    translation_domain=DOMAIN,
                    translation_key="preset_change_failed",
                ) from err
