"""Shared contract objects for integration tests."""

from __future__ import annotations

import json
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock

from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.advanced_shot_stopper.const import (
    CONF_DEVICE_ID,
    CONF_WEBHOOK_ID,
    DOMAIN,
)
from custom_components.advanced_shot_stopper.coordinator import CoordinatorData
from custom_components.advanced_shot_stopper.models import DeviceSnapshot, PresetState

FIXTURES = Path(__file__).parents[3] / "src" / "tests" / "fixtures"
WEBHOOK_ID = "webhook_id_with_more_than_32_chars_123456"


def fixture(name: str) -> dict:
    """Load a firmware-owned JSON fixture."""
    return json.loads((FIXTURES / name).read_text())


def config_entry(**updates) -> MockConfigEntry:
    """Build one configured controller entry."""
    data = {
        "host": "stopper.local",
        CONF_DEVICE_ID: "AA:BB:CC:DD:EE:FF",
        CONF_WEBHOOK_ID: WEBHOOK_ID,
    }
    data.update(updates)
    return MockConfigEntry(domain=DOMAIN, data=data, unique_id=data[CONF_DEVICE_ID])


def coordinator_data() -> CoordinatorData:
    """Build authoritative data from shared fixtures."""
    return CoordinatorData(
        DeviceSnapshot.from_dict(fixture("integration_snapshot.json")),
        PresetState.from_dict(fixture("integration_presets.json")),
        None,
        None,
    )


def api_mock() -> MagicMock:
    """Build a fully asynchronous API double."""
    api = MagicMock()
    api.host = "stopper.local"
    for method in (
        "async_snapshot",
        "async_presets",
        "async_webhook_config",
        "async_configure_webhook",
        "async_test_webhook",
        "async_select_preset",
        "async_apply_webhook_config",
    ):
        setattr(api, method, AsyncMock())
    api.async_snapshot.return_value = coordinator_data().snapshot
    api.async_presets.return_value = coordinator_data().presets
    api.async_webhook_config.return_value = {
        "enabled": False,
        "url": "",
        "brewState": False,
        "firstDrop": False,
        "end": False,
        "presetChanges": False,
        "deferDuringShot": False,
    }
    return api
