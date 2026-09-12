"""Sensors for Advanced Shot Stopper."""

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
from homeassistant.const import UnitOfMass, UnitOfTime
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback
from homeassistant.helpers.typing import StateType

from .coordinator import ShotStopperCoordinator
from .entity import ShotStopperEntity
from .models import Shot
from .runtime import ShotStopperRuntimeData

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
    entry: ConfigEntry[ShotStopperRuntimeData],
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Create all native sensors."""
    runtime: ShotStopperRuntimeData = entry.runtime_data
    async_add_entities(
        [
            ShotStateSensor(runtime.coordinator),
            *(
                StoredShotSensor(runtime.coordinator, description)
                for description in SHOT_DESCRIPTIONS
            ),
        ]
    )


class ShotStateSensor(ShotStopperEntity, SensorEntity):
    """Current brewing state."""

    _attr_translation_key = "shot_state"
    _attr_icon = "mdi:coffee-maker"
    _attr_device_class = SensorDeviceClass.ENUM

    def __init__(self, coordinator: ShotStopperCoordinator) -> None:
        super().__init__(coordinator, "shot_state")
        self._attr_options = ["idle", "brewing"]

    @property
    def native_value(self) -> str:
        return self.coordinator.data.snapshot.shot_state


class StoredShotSensor(ShotStopperEntity, SensorEntity):
    """One value from the last or last-good shot."""

    entity_description: ShotDescription

    def __init__(
        self, coordinator: ShotStopperCoordinator, description: ShotDescription
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
