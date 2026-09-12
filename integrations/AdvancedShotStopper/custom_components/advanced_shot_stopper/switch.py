"""Home Quick Settings switches for Advanced Shot Stopper."""

from __future__ import annotations

from dataclasses import dataclass

from homeassistant.components.switch import SwitchEntity, SwitchEntityDescription
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_platform import AddConfigEntryEntitiesCallback

from .api import ApiError
from .const import DOMAIN
from .coordinator import ShotStopperCoordinator
from .entity import ShotStopperEntity
from .models import ProtocolError, QuickSettings
from .runtime import ShotStopperRuntimeData

PARALLEL_UPDATES = 1


@dataclass(frozen=True, kw_only=True)
class QuickSettingDescription(SwitchEntityDescription):
    """Map one switch to a complete Quick Settings field."""

    api_field: str
    state_field: str
    bbw_dependent: bool = False


DESCRIPTIONS = (
    QuickSettingDescription(
        key="brew_by_weight",
        translation_key="brew_by_weight",
        api_field="brewByWeight",
        state_field="brew_by_weight",
        icon="mdi:scale",
    ),
    QuickSettingDescription(
        key="no_scale_bbw",
        translation_key="no_scale_bbw",
        api_field="noScaleBbwMode",
        state_field="no_scale_bbw_mode",
        icon="mdi:scale-off",
        bbw_dependent=True,
    ),
    *(
        QuickSettingDescription(
            key=key,
            translation_key=key,
            api_field=api_field,
            state_field=state_field,
            bbw_dependent=True,
            icon=icon,
        )
        for key, api_field, state_field, icon in (
            (
                "auto_to_manual_guard",
                "autoToManualGuardEnabled",
                "auto_to_manual_guard_enabled",
                "mdi:swap-horizontal",
            ),
            (
                "slow_extraction_guard",
                "slowExtractionGuardEnabled",
                "slow_extraction_guard_enabled",
                "mdi:speedometer-slow",
            ),
            (
                "fast_extraction_guard",
                "fastExtractionGuardEnabled",
                "fast_extraction_guard_enabled",
                "mdi:speedometer",
            ),
            (
                "avoid_accidental_touch",
                "avoidAccidentalTouchEnabled",
                "avoid_accidental_touch_enabled",
                "mdi:gesture-tap-button",
            ),
            (
                "cup_protection",
                "cupProtectionEnabled",
                "cup_protection_enabled",
                "mdi:cup-water",
            ),
        )
    ),
)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry[ShotStopperRuntimeData],
    async_add_entities: AddConfigEntryEntitiesCallback,
) -> None:
    """Create all seven confirmed-state switches."""
    coordinator = entry.runtime_data.coordinator
    async_add_entities(QuickSettingSwitch(coordinator, item) for item in DESCRIPTIONS)


class QuickSettingSwitch(ShotStopperEntity, SwitchEntity):
    """A non-optimistic controller-owned Quick Setting."""

    entity_description: QuickSettingDescription

    def __init__(
        self, coordinator: ShotStopperCoordinator, description: QuickSettingDescription
    ) -> None:
        super().__init__(coordinator, description.key)
        self.entity_description = description
        self._last_non_off_mode = "warn_once"

    @property
    def _settings(self) -> QuickSettings:
        return self.coordinator.data.snapshot.quick_settings

    @property
    def available(self) -> bool:
        return (
            super().available
            and self.coordinator.data.snapshot.shot_state == "idle"
            and (
                not self.entity_description.bbw_dependent
                or self._settings.brew_by_weight
            )
        )

    @property
    def is_on(self) -> bool:
        value: bool | str = getattr(
            self._settings, self.entity_description.state_field
        )
        if self.entity_description.api_field == "noScaleBbwMode":
            if isinstance(value, str) and value != "off":
                self._last_non_off_mode = value
            return value != "off"
        return value is True

    async def async_turn_on(self, **kwargs: object) -> None:
        """Request ON and publish only the confirmed refresh."""
        value: bool | str = (
            self._last_non_off_mode
            if self.entity_description.api_field == "noScaleBbwMode"
            else True
        )
        await self._async_set(value)

    async def async_turn_off(self, **kwargs: object) -> None:
        """Request OFF and publish only the confirmed refresh."""
        value: bool | str = (
            "off" if self.entity_description.api_field == "noScaleBbwMode" else False
        )
        await self._async_set(value)

    async def _async_set(self, value: bool | str) -> None:
        revision = self._settings.revision
        try:
            await self.coordinator.async_confirmed_command(
                lambda: self.coordinator.api.async_set_quick_setting(
                    self.entity_description.api_field, value, revision
                )
            )
        except (ApiError, ProtocolError, TimeoutError) as err:
            raise HomeAssistantError(
                translation_domain=DOMAIN,
                translation_key="quick_setting_change_failed",
            ) from err
