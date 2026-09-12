"""Base entity for Advanced Shot Stopper."""

from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import ShotStopperCoordinator


class ShotStopperEntity(CoordinatorEntity[ShotStopperCoordinator]):
    """Associate every entity with the one controller device."""

    _attr_has_entity_name = True

    def __init__(self, coordinator: ShotStopperCoordinator, key: str) -> None:
        super().__init__(coordinator)
        snapshot = coordinator.data.snapshot
        self._attr_unique_id = f"{snapshot.device_id}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, snapshot.device_id)},
            name="Advanced Shot Stopper",
            manufacturer=snapshot.manufacturer,
            model=snapshot.model,
            sw_version=snapshot.firmware_version,
            configuration_url=f"http://{coordinator.api.host}/",
        )
