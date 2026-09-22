"""Base entity for Open Brew by Weight."""

from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .const import DOMAIN
from .coordinator import OpenBrewByWeightCoordinator
from .models import DEFAULT_MANUFACTURER, DEFAULT_MODEL, DeviceSnapshot


def _configuration_url(host: str, mdns_host: str | None) -> str:
    """Prefer the announced .local name; fall back to the setup host."""
    if mdns_host:
        return f"http://{mdns_host}.local/"
    return f"http://{host}/"


def _connections(
    snapshot: DeviceSnapshot,
) -> set[tuple[str, str]]:
    """Report the controller's WiFi and Bluetooth addresses when announced."""
    connections: set[tuple[str, str]] = set()
    if snapshot.wifi_mac:
        connections.add((dr.CONNECTION_NETWORK_MAC, snapshot.wifi_mac))
    if snapshot.bluetooth_mac:
        connections.add((dr.CONNECTION_BLUETOOTH, snapshot.bluetooth_mac))
    return connections


class OpenBrewByWeightEntity(CoordinatorEntity[OpenBrewByWeightCoordinator]):
    """Associate every entity with the one controller device."""

    _attr_has_entity_name = True

    def __init__(self, coordinator: OpenBrewByWeightCoordinator, key: str) -> None:
        super().__init__(coordinator)
        snapshot = coordinator.data.snapshot
        self._attr_unique_id = f"{snapshot.device_id}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, snapshot.device_id)},
            name=snapshot.model or DEFAULT_MODEL,
            manufacturer=snapshot.manufacturer or DEFAULT_MANUFACTURER,
            model=snapshot.model or DEFAULT_MODEL,
            hw_version=snapshot.hardware_profile or None,
            sw_version=snapshot.firmware_version,
            configuration_url=_configuration_url(
                coordinator.api.host, snapshot.mdns_host
            ),
            connections=_connections(snapshot),
        )
