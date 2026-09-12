"""Push-first state coordinator for Advanced Shot Stopper."""

from __future__ import annotations

import asyncio
from collections import deque
from dataclasses import dataclass, replace
from typing import Any

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.storage import Store
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import ShotStopperApi
from .const import DOMAIN, STORE_KEY_PREFIX, STORE_VERSION, UPDATE_INTERVAL
from .models import DeviceSnapshot, PresetState, ProtocolError, Shot, WebhookEvent


@dataclass(frozen=True, slots=True)
class CoordinatorData:
    """State shared by every entity."""

    snapshot: DeviceSnapshot
    presets: PresetState
    last_shot: Shot | None
    last_good_shot: Shot | None


class ShotStopperCoordinator(DataUpdateCoordinator[CoordinatorData]):
    """Reconcile REST snapshots and validated webhook pushes."""

    def __init__(
        self,
        hass: HomeAssistant,
        entry: ConfigEntry,
        device_id: str,
        api: ShotStopperApi,
    ) -> None:
        super().__init__(
            hass,
            logger=__import__("logging").getLogger(__name__),
            config_entry=entry,
            name=DOMAIN,
            update_interval=UPDATE_INTERVAL,
        )
        self.api = api
        self.device_id = device_id
        self.command_lock = asyncio.Lock()
        self._store = Store[dict[str, Any]](
            hass, STORE_VERSION, f"{STORE_KEY_PREFIX}.{entry.entry_id}"
        )
        self._stored_last: Shot | None = None
        self._stored_good: Shot | None = None
        self._seen_order: deque[tuple[str, int, int, str, int]] = deque(maxlen=256)
        self._seen: set[tuple[str, int, int, str, int]] = set()
        self._last_uptime_by_boot: dict[int, int] = {}
        self._test_waiters: dict[str, asyncio.Future[None]] = {}

    async def async_load_store(self) -> None:
        """Restore the compact shot aggregate, ignoring corrupt local data."""
        stored = await self._store.async_load()
        if not isinstance(stored, dict):
            return
        try:
            self._stored_last = (
                None
                if stored.get("last_shot") is None
                else Shot.from_dict(stored["last_shot"])
            )
            self._stored_good = (
                None
                if stored.get("last_good_shot") is None
                else Shot.from_dict(stored["last_good_shot"])
            )
        except KeyError, TypeError, ValueError:
            self._stored_last = self._stored_good = None

    async def _async_update_data(self) -> CoordinatorData:
        try:
            snapshot, presets = await asyncio.gather(
                self.api.async_snapshot(), self.api.async_presets()
            )
        except Exception as err:
            raise UpdateFailed(str(err)) from err
        if snapshot.device_id != self.device_id:
            raise UpdateFailed("controller identity changed")
        last = self._stored_last
        good = self._stored_good
        if snapshot.last_shot is not None and (
            last is None or snapshot.last_shot.uptime_ms > last.uptime_ms
        ):
            last = snapshot.last_shot
            if self._is_good(last):
                good = last
            self._stored_last, self._stored_good = last, good
            self._store.async_delay_save(self._storage_data, 1)
        return CoordinatorData(snapshot, presets, last, good)

    @staticmethod
    def _is_good(shot: Shot) -> bool:
        return shot.duration_ms > 12000 and shot.weight_g is not None and shot.weight_g > 2

    def expect_test(self, correlation_id: str) -> asyncio.Future[None]:
        """Create the waiter before requesting a test callback."""
        future = self.hass.loop.create_future()
        self._test_waiters[correlation_id] = future
        return future

    def cancel_test(self, correlation_id: str) -> None:
        """Remove a test waiter after timeout or rollback."""
        if future := self._test_waiters.pop(correlation_id, None):
            future.cancel()

    async def async_process_webhook(self, event: WebhookEvent) -> None:
        """Apply one valid, ordered controller event."""
        if event.device_id != self.device_id:
            raise PermissionError
        key = event.deduplication_key
        if key in self._seen:
            return
        latest = self._last_uptime_by_boot.get(event.boot_id, -1)
        if event.uptime_ms < latest:
            return
        if event.event == "test":
            correlation = event.data.get("correlationId")
            if (
                isinstance(correlation, str)
                and (future := self._test_waiters.pop(correlation, None))
                and not future.done()
            ):
                future.set_result(None)
            self._accept_event(key, event)
            return
        if self.data is None:
            return
        data = self.data
        if event.event == "brew_state":
            state = event.data.get("state")
            if state not in ("idle", "brewing"):
                raise ProtocolError("state is invalid")
            self._accept_event(key, event)
            self.async_set_updated_data(
                replace(data, snapshot=replace(data.snapshot, shot_state=state))
            )
            return
        if event.event == "end":
            shot = Shot.from_dict(event.data)
            good = shot if self._is_good(shot) else data.last_good_shot
            self._accept_event(key, event)
            self._stored_last, self._stored_good = shot, good
            self._store.async_delay_save(self._storage_data, 1)
            self.async_set_updated_data(
                replace(data, last_shot=shot, last_good_shot=good)
            )
            return
        if event.event == "presets_changed":
            payload = {**event.data, "apiVersion": 1}
            presets = PresetState.from_dict(payload)
            if presets.revision <= data.presets.revision:
                return
            self._accept_event(key, event)
            if presets.revision > data.presets.revision + 1:
                self.hass.async_create_task(self.async_request_refresh())
            snapshot = replace(
                data.snapshot,
                active_preset_id=presets.active_id,
                preset_revision=presets.revision,
            )
            self.async_set_updated_data(
                replace(data, snapshot=snapshot, presets=presets)
            )

    def _accept_event(
        self, key: tuple[str, int, int, str, int], event: WebhookEvent
    ) -> None:
        if len(self._seen_order) == self._seen_order.maxlen:
            self._seen.discard(self._seen_order[0])
        self._seen_order.append(key)
        self._seen.add(key)
        self._last_uptime_by_boot[event.boot_id] = event.uptime_ms

    def _storage_data(self) -> dict[str, Any]:
        return {
            "last_shot": None
            if self._stored_last is None
            else self._stored_last.to_dict(),
            "last_good_shot": None
            if self._stored_good is None
            else self._stored_good.to_dict(),
        }
