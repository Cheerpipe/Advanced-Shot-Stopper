"""Controller restart button for Advanced Shot Stopper."""

from homeassistant.components.button import ButtonDeviceClass, ButtonEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory
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
    """Create the safe queued restart button."""
    async_add_entities([RestartButton(entry.runtime_data.coordinator)])


class RestartButton(ShotStopperEntity, ButtonEntity):
    """Queue the controller's existing shot-safe restart path."""

    _attr_translation_key = "restart"
    _attr_device_class = ButtonDeviceClass.RESTART
    _attr_entity_category = EntityCategory.CONFIG

    def __init__(self, coordinator: ShotStopperCoordinator) -> None:
        super().__init__(coordinator, "restart")

    async def async_press(self) -> None:
        """Send restart once; the controller intentionally disconnects."""
        try:
            await self.coordinator.async_confirmed_command(
                self.coordinator.api.async_restart, restarting=True
            )
        except (ApiError, ProtocolError, TimeoutError) as err:
            raise HomeAssistantError(
                translation_domain=DOMAIN,
                translation_key="restart_failed",
            ) from err
