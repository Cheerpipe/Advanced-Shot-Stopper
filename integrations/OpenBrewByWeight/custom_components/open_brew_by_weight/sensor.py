"""Sensors for Open Brew by Weight."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import EntityCategory, UnitOfMass, UnitOfTime
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.typing import StateType

from .coordinator import OpenBrewByWeightCoordinator
from .entity import OpenBrewByWeightEntity
from .models import Shot
from .runtime import OpenBrewByWeightRuntimeData

PARALLEL_UPDATES = 1


@dataclass(frozen=True, kw_only=True)
class ShotDescription(SensorEntityDescription):
    """Description of a stored-shot sensor."""

    source: str
    value_fn: Callable[[Shot], StateType]


def _seconds(field: str) -> Callable[[Shot], float | None]:
    return lambda shot: (
        None if getattr(shot, field) is None else getattr(shot, field) / 1000
    )


def _value(field: str) -> Callable[[Shot], StateType]:
    return lambda shot: getattr(shot, field)


SHOT_DESCRIPTIONS = tuple(
    ShotDescription(
        key=f"{source}_{key}",
        translation_key=f"{source}_{key}",
        source=source,
        value_fn=value_fn,
        icon="mdi:water-check"
        if source == "last_good_shot" and key == "first_drop"
        else icon,
        device_class=device_class,
        native_unit_of_measurement=unit,
        state_class=state_class,
    )
    for source in ("last_shot", "last_good_shot")
    for key, value_fn, icon, device_class, unit, state_class in (
        (
            "duration",
            _seconds("duration_ms"),
            "mdi:timer-outline",
            SensorDeviceClass.DURATION,
            UnitOfTime.SECONDS,
            SensorStateClass.MEASUREMENT,
        ),
        (
            "final_weight",
            _value("weight_g"),
            "mdi:scale",
            SensorDeviceClass.WEIGHT,
            UnitOfMass.GRAMS,
            SensorStateClass.MEASUREMENT,
        ),
        (
            "target_weight",
            _value("target_weight_g"),
            "mdi:target",
            SensorDeviceClass.WEIGHT,
            UnitOfMass.GRAMS,
            SensorStateClass.MEASUREMENT,
        ),
        (
            "average_flow",
            _value("average_flow_gps"),
            "mdi:waves-arrow-right",
            None,
            "g/s",
            SensorStateClass.MEASUREMENT,
        ),
        (
            "first_drop",
            _seconds("first_drop_ms"),
            "mdi:water-outline",
            SensorDeviceClass.DURATION,
            UnitOfTime.SECONDS,
            SensorStateClass.MEASUREMENT,
        ),
        ("type", _value("shot_type"), "mdi:coffee", SensorDeviceClass.ENUM, None, None),
        (
            "stop_detail",
            _value("stop_detail"),
            "mdi:stop-circle-outline",
            SensorDeviceClass.ENUM,
            None,
            None,
        ),
        ("preset", _value("preset_name"), "mdi:tune-variant", None, None, None),
    )
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry[OpenBrewByWeightRuntimeData],
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Create all native sensors."""
    runtime: OpenBrewByWeightRuntimeData = entry.runtime_data
    async_add_entities(
        [
            ShotStateSensor(runtime.coordinator),
            ControllerSensor(runtime.coordinator),
            MachineSensor(runtime.coordinator),
            IpSensor(runtime.coordinator),
            *(
                StoredShotSensor(runtime.coordinator, description)
                for description in SHOT_DESCRIPTIONS
            ),
        ]
    )


class ControllerSensor(OpenBrewByWeightEntity, SensorEntity):
    """Controller hardware identity, read once from the initial snapshot."""

    _attr_translation_key = "controller"
    _attr_icon = "mdi:chip"
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator: OpenBrewByWeightCoordinator) -> None:
        super().__init__(coordinator, "controller")
        self._attr_native_value = coordinator.data.snapshot.hardware_profile


class MachineSensor(OpenBrewByWeightEntity, SensorEntity):
    """Espresso machine identity, read once from the initial snapshot."""

    _attr_translation_key = "machine"
    _attr_icon = "mdi:coffee-maker"
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator: OpenBrewByWeightCoordinator) -> None:
        super().__init__(coordinator, "machine")
        snapshot = coordinator.data.snapshot
        profile = snapshot.machine_profile
        self._attr_native_value = (
            f"{snapshot.machine_name} ({profile})" if profile else snapshot.machine_name
        )


class IpSensor(OpenBrewByWeightEntity, SensorEntity):
    """Current controller IP; webhook pushes refresh it on DHCP change."""

    _attr_translation_key = "ip"
    _attr_icon = "mdi:ip"
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator: OpenBrewByWeightCoordinator) -> None:
        super().__init__(coordinator, "ip")

    @property
    def native_value(self) -> StateType:
        return self.coordinator.data.snapshot.ip


class ShotStateSensor(OpenBrewByWeightEntity, SensorEntity):
    """Current brewing state."""

    _attr_translation_key = "shot_state"
    _attr_icon = "mdi:coffee-maker"
    _attr_device_class = SensorDeviceClass.ENUM

    def __init__(self, coordinator: OpenBrewByWeightCoordinator) -> None:
        super().__init__(coordinator, "shot_state")
        self._attr_options = ["idle", "brewing"]

    @property
    def native_value(self) -> str:
        return self.coordinator.data.snapshot.shot_state


class StoredShotSensor(OpenBrewByWeightEntity, SensorEntity):
    """One value from the last or last-good shot."""

    entity_description: ShotDescription

    def __init__(
        self, coordinator: OpenBrewByWeightCoordinator, description: ShotDescription
    ) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description
        if description.key.endswith("_type"):
            self._attr_options = ["auto", "timer_only", "manual"]
        elif description.key.endswith("_stop_detail"):
            from .const import STOP_DETAILS

            self._attr_options = list(STOP_DETAILS)

    @property
    def native_value(self) -> StateType:
        shot = getattr(self.coordinator.data, self.entity_description.source)
        return None if shot is None else self.entity_description.value_fn(shot)
