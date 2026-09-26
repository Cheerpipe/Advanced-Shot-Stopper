"""Sensors for Open Brew by Weight."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from datetime import UTC, datetime

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

from .const import ACTIVATION_TYPES, STOP_DETAILS
from .coordinator import CoordinatorData, OpenBrewByWeightCoordinator
from .entity import OpenBrewByWeightEntity
from .models import LastActivation, Shot
from .runtime import OpenBrewByWeightRuntimeData

PARALLEL_UPDATES = 1
SensorValue = StateType | datetime


@dataclass(frozen=True, kw_only=True)
class MirroredDescription(SensorEntityDescription):
    """Description of one mirrored WebUI value."""

    value_fn: Callable[[CoordinatorData], SensorValue]
    options: list[str] | None = None


def _shot(
    value_fn: Callable[[Shot], SensorValue],
) -> Callable[[CoordinatorData], SensorValue]:
    return lambda data: None if data.last_shot is None else value_fn(data.last_shot)


def _activation(
    value_fn: Callable[[LastActivation], SensorValue],
) -> Callable[[CoordinatorData], SensorValue]:
    return lambda data: (
        None if data.last_activation is None else value_fn(data.last_activation)
    )


def _stats(field: str) -> Callable[[CoordinatorData], StateType]:
    return lambda data: None if data.stats is None else getattr(data.stats, field)


def _seconds(
    value_fn: Callable[[Shot], int | None],
) -> Callable[[Shot], float | None]:
    def converted(source: Shot) -> float | None:
        value = value_fn(source)
        return None if value is None else value / 1000

    return converted


MIRRORED_DESCRIPTIONS = (
    # Last shot: a mirror of the WebUI last-shot card.
    *(
        MirroredDescription(
            key=f"last_shot_{key}",
            translation_key=f"last_shot_{key}",
            value_fn=_shot(value_fn),
            icon=icon,
            device_class=device_class,
            native_unit_of_measurement=unit,
            state_class=state_class,
            options=options,
        )
        for key, value_fn, icon, device_class, unit, state_class, options in (
            (
                "time",
                lambda shot: datetime.fromtimestamp(shot.ended_at_unix_sec, tz=UTC)
                if shot.ended_at_unix_sec
                else None,
                "mdi:clock-outline",
                SensorDeviceClass.TIMESTAMP,
                None,
                None,
                None,
            ),
            (
                "duration",
                _seconds(lambda shot: shot.duration_ms),
                "mdi:timer-outline",
                SensorDeviceClass.DURATION,
                UnitOfTime.SECONDS,
                SensorStateClass.MEASUREMENT,
                None,
            ),
            (
                "final_weight",
                lambda shot: shot.weight_g,
                "mdi:scale",
                SensorDeviceClass.WEIGHT,
                UnitOfMass.GRAMS,
                SensorStateClass.MEASUREMENT,
                None,
            ),
            (
                "target_weight",
                lambda shot: shot.target_weight_g,
                "mdi:target",
                SensorDeviceClass.WEIGHT,
                UnitOfMass.GRAMS,
                SensorStateClass.MEASUREMENT,
                None,
            ),
            (
                "average_flow",
                lambda shot: shot.average_flow_gps,
                "mdi:waves-arrow-right",
                None,
                "g/s",
                SensorStateClass.MEASUREMENT,
                None,
            ),
            (
                "first_drop",
                _seconds(lambda shot: shot.first_drop_ms),
                "mdi:water-outline",
                SensorDeviceClass.DURATION,
                UnitOfTime.SECONDS,
                SensorStateClass.MEASUREMENT,
                None,
            ),
            (
                "rating",
                lambda shot: shot.rating if shot.rating is not None else "Unrated",
                "mdi:star",
                None,
                None,
                None,
                None,
            ),
            (
                "type",
                lambda shot: shot.shot_type,
                "mdi:coffee",
                SensorDeviceClass.ENUM,
                None,
                None,
                ["auto", "timer_only", "manual"],
            ),
            (
                "stop_detail",
                lambda shot: shot.stop_detail,
                "mdi:stop-circle-outline",
                SensorDeviceClass.ENUM,
                None,
                None,
                list(STOP_DETAILS),
            ),
            (
                "preset",
                lambda shot: shot.preset_name,
                "mdi:tune-variant",
                None,
                None,
                None,
                None,
            ),
        )
    ),
    # Last activation: a mirror of the newest WebUI History entry.
    MirroredDescription(
        key="last_activation_time",
        translation_key="last_activation_time",
        value_fn=_activation(
            lambda record: (
                None
                if not record.has_wall_time or record.ended_at_unix_sec == 0
                else datetime.fromtimestamp(record.ended_at_unix_sec, tz=UTC)
            )
        ),
        icon="mdi:history",
        device_class=SensorDeviceClass.TIMESTAMP,
    ),
    MirroredDescription(
        key="last_activation_type",
        translation_key="last_activation_type",
        value_fn=_activation(lambda record: record.type),
        icon="mdi:gesture-tap",
        device_class=SensorDeviceClass.ENUM,
        options=list(ACTIVATION_TYPES),
    ),
    MirroredDescription(
        key="last_activation_duration",
        translation_key="last_activation_duration",
        value_fn=_activation(lambda record: record.duration_s),
        icon="mdi:timer-outline",
        device_class=SensorDeviceClass.DURATION,
        native_unit_of_measurement=UnitOfTime.SECONDS,
        state_class=SensorStateClass.MEASUREMENT,
    ),
    # Stats: a mirror of the firmware-computed Stats card averages.
    MirroredDescription(
        key="stats_shot_count",
        translation_key="stats_shot_count",
        value_fn=_stats("shot_count"),
        icon="mdi:counter",
        state_class=SensorStateClass.MEASUREMENT,
    ),
    MirroredDescription(
        key="stats_avg_duration",
        translation_key="stats_avg_duration",
        value_fn=_stats("avg_duration_s"),
        icon="mdi:timer-outline",
        device_class=SensorDeviceClass.DURATION,
        native_unit_of_measurement=UnitOfTime.SECONDS,
        state_class=SensorStateClass.MEASUREMENT,
    ),
    MirroredDescription(
        key="stats_avg_yield",
        translation_key="stats_avg_yield",
        value_fn=_stats("avg_yield_g"),
        icon="mdi:scale",
        device_class=SensorDeviceClass.WEIGHT,
        native_unit_of_measurement=UnitOfMass.GRAMS,
        state_class=SensorStateClass.MEASUREMENT,
    ),
    MirroredDescription(
        key="stats_avg_error",
        translation_key="stats_avg_error",
        value_fn=_stats("avg_error_pct"),
        icon="mdi:percent",
        native_unit_of_measurement="%",
        state_class=SensorStateClass.MEASUREMENT,
    ),
    MirroredDescription(
        key="stats_avg_flow",
        translation_key="stats_avg_flow",
        value_fn=_stats("avg_flow_gps"),
        icon="mdi:waves-arrow-right",
        native_unit_of_measurement="g/s",
        state_class=SensorStateClass.MEASUREMENT,
    ),
    MirroredDescription(
        key="stats_shots_per_day",
        translation_key="stats_shots_per_day",
        value_fn=_stats("shots_per_day"),
        icon="mdi:calendar-clock",
        state_class=SensorStateClass.MEASUREMENT,
    ),
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
                MirroredSensor(runtime.coordinator, description)
                for description in MIRRORED_DESCRIPTIONS
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

    @property
    def extra_state_attributes(self) -> dict[str, StateType]:
        """Static identity details captured from the initial snapshot."""
        snapshot = self.coordinator.data.snapshot
        return {
            "arch": snapshot.arch,
            "firmware_version": snapshot.firmware_version,
        }


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


class MirroredSensor(OpenBrewByWeightEntity, SensorEntity):
    """One value mirrored from the WebUI last shot, history, or stats view."""

    entity_description: MirroredDescription

    def __init__(
        self, coordinator: OpenBrewByWeightCoordinator, description: MirroredDescription
    ) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description
        if description.options is not None:
            self._attr_options = description.options

    @property
    def native_value(self) -> SensorValue:
        return self.entity_description.value_fn(self.coordinator.data)
