"""Read-only Brew by weight state for Open Brew by Weight."""

from __future__ import annotations

from homeassistant.components.binary_sensor import BinarySensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .const import DOMAIN
from .coordinator import OpenBrewByWeightCoordinator
from .entity import OpenBrewByWeightEntity
from .runtime import OpenBrewByWeightRuntimeData

PARALLEL_UPDATES = 0


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry[OpenBrewByWeightRuntimeData],
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Create the Brew by weight state and retire the former switch."""
    sensor = BrewByWeightSensor(entry.runtime_data.coordinator)
    registry = er.async_get(hass)
    if retired := registry.async_get_entity_id("switch", DOMAIN, sensor.unique_id or ""):
        registry.async_remove(retired)
    async_add_entities([sensor])


class BrewByWeightSensor(OpenBrewByWeightEntity, BinarySensorEntity):
    """Whether the active preset stops shots by weight (Manual is a preset)."""

    _attr_translation_key = "brew_by_weight"

    def __init__(self, coordinator: OpenBrewByWeightCoordinator) -> None:
        super().__init__(coordinator, "brew_by_weight")

    @property
    def is_on(self) -> bool:
        return self.coordinator.data.snapshot.quick_settings.brew_by_weight
