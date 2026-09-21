"""Base entity for Open Brew by Weight."""

from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import OpenBrewByWeightCoordinator


class OpenBrewByWeightEntity(CoordinatorEntity[OpenBrewByWeightCoordinator]):
    """Associate every entity with the one controller device."""

    _attr_has_entity_name = True

    def __init__(self, coordinator: OpenBrewByWeightCoordinator, key: str) -> None:
        super().__init__(coordinator)
        snapshot = coordinator.data.snapshot
        self._attr_unique_id = f"{snapshot.device_id}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, snapshot.device_id)},
            name="Open Brew by Weight",
            manufacturer=snapshot.manufacturer,
            model=snapshot.model,
            sw_version=snapshot.firmware_version,
            configuration_url=f"http://{coordinator.api.host}/",
        )
