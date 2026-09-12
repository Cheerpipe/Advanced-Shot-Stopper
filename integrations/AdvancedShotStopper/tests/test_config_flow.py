"""Config-flow and reconfiguration tests."""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import pytest
from homeassistant import config_entries
from homeassistant.const import CONF_HOST
from homeassistant.data_entry_flow import FlowResultType

from custom_components.advanced_shot_stopper.api import (
    CannotConnect,
    IncompatibleApi,
    RequestRejected,
)
from custom_components.advanced_shot_stopper.const import (
    CONF_WEBHOOK_ID,
    DOMAIN,
)
from custom_components.advanced_shot_stopper.models import ProtocolError
from custom_components.advanced_shot_stopper.runtime import webhook_url

from .helpers import WEBHOOK_ID, api_mock, config_entry, coordinator_data, fixture

pytestmark = pytest.mark.usefixtures("enable_custom_integrations")


def _configure_api(api: MagicMock, *, remote_url: str = "") -> None:
    api.async_snapshot = AsyncMock(return_value=coordinator_data().snapshot)
    api.async_webhook_config = AsyncMock(
        return_value={
            "enabled": bool(remote_url),
            "url": remote_url,
            "brewState": bool(remote_url),
            "firstDrop": bool(remote_url),
            "end": bool(remote_url),
            "presetChanges": bool(remote_url),
            "deferDuringShot": False,
        }
    )


async def _start(hass, api: MagicMock):
    with (
        patch(
            "custom_components.advanced_shot_stopper.config_flow.ShotStopperApi",
            return_value=api,
        ),
        patch(
            "custom_components.advanced_shot_stopper.config_flow.webhook_url",
            return_value="http://homeassistant.local/api/webhook/candidate",
        ),
        patch(
            "custom_components.advanced_shot_stopper.config_flow.webhook.async_generate_id",
            return_value=WEBHOOK_ID,
        ),
    ):
        initial = await hass.config_entries.flow.async_init(
            DOMAIN, context={"source": config_entries.SOURCE_USER}
        )
        assert initial["type"] is FlowResultType.FORM
        return await hass.config_entries.flow.async_configure(
            initial["flow_id"], {CONF_HOST: "stopper.local"}
        )


async def test_user_flow_success_and_one_field(hass) -> None:
    """The initial form accepts only a host and creates one controller entry."""
    api = api_mock()
    _configure_api(api)
    result = await _start(hass, api)
    assert result["type"] is FlowResultType.CREATE_ENTRY
    assert result["data"][CONF_HOST] == "stopper.local"
    assert result["data"][CONF_WEBHOOK_ID] == WEBHOOK_ID


@pytest.mark.parametrize(
    ("failure", "error"),
    [
        (CannotConnect(), "cannot_connect"),
        (IncompatibleApi(), "incompatible_api"),
        (RequestRejected("BAD_RESPONSE", 409), "cannot_connect"),
        (ProtocolError("bad response"), "cannot_connect"),
        (ValueError(), "invalid_host"),
    ],
)
async def test_user_flow_maps_errors(hass, failure, error: str) -> None:
    """Every setup failure is actionable."""
    api = api_mock()
    _configure_api(api)
    if isinstance(failure, ValueError):
        api.async_webhook_config.side_effect = failure
    else:
        api.async_snapshot.side_effect = failure
    result = await _start(hass, api)
    assert result["type"] is FlowResultType.FORM
    assert result["errors"] == {"base": error}


def test_callback_url_requires_plain_http(hass) -> None:
    """Only controller-reachable, credential-free HTTP callback URLs are allowed."""
    with patch(
        "custom_components.advanced_shot_stopper.config_flow.webhook.async_generate_url",
        return_value="http://home.local/api/webhook/id",
    ) as generate:
        assert webhook_url(hass, WEBHOOK_ID).startswith("http://")
    assert generate.call_args.kwargs == {
        "allow_external": False,
        "allow_ip": True,
        "prefer_external": False,
    }
    for value in (
        "https://home.local/hook",
        "http://user:pass@home.local/hook",
        "/hook",
    ):
        with (
            patch(
                "custom_components.advanced_shot_stopper.config_flow.webhook.async_generate_url",
                return_value=value,
            ),
            pytest.raises(ValueError),
        ):
            webhook_url(hass, WEBHOOK_ID)


async def test_duplicate_device_aborts(hass) -> None:
    """Stable device identity prevents duplicate entries."""
    existing = config_entry()
    existing.add_to_hass(hass)
    api = api_mock()
    _configure_api(api)
    result = await _start(hass, api)
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "already_configured"


async def test_takeover_requires_confirmation(hass) -> None:
    """An existing destination is never silently overwritten."""
    api = api_mock()
    _configure_api(api, remote_url="http://other/receiver")
    result = await _start(hass, api)
    assert result["step_id"] == "takeover"
    rejected = await hass.config_entries.flow.async_configure(
        result["flow_id"], {"confirm": False}
    )
    assert rejected["reason"] == "takeover_rejected"

    api = api_mock()
    _configure_api(api, remote_url="http://other/receiver")
    result = await _start(hass, api)
    accepted = await hass.config_entries.flow.async_configure(
        result["flow_id"], {"confirm": True}
    )
    assert accepted["type"] is FlowResultType.CREATE_ENTRY


async def test_reconfigure_reapply_rotate_and_rollback(hass) -> None:
    """Reconfiguration tests before committing host or webhook changes."""
    entry = config_entry()
    entry.add_to_hass(hass)
    api = api_mock()
    _configure_api(api)
    runtime = MagicMock()
    runtime.api = api
    runtime.coordinator.api = api
    runtime.async_configure_and_test = AsyncMock()
    entry.runtime_data = runtime
    new_id = "replacement_webhook_id_with_32_chars_123"
    with (
        patch(
            "custom_components.advanced_shot_stopper.config_flow.ShotStopperApi",
            return_value=api,
        ),
        patch(
            "custom_components.advanced_shot_stopper.config_flow.webhook_url",
            side_effect=lambda _hass, value: f"http://home/api/webhook/{value}",
        ),
    ):
        result = await hass.config_entries.flow.async_init(
            DOMAIN,
            context={
                "source": config_entries.SOURCE_RECONFIGURE,
                "entry_id": entry.entry_id,
            },
        )
        assert result["description_placeholders"]["webhook_url"].endswith(WEBHOOK_ID)
        result = await hass.config_entries.flow.async_configure(
            result["flow_id"], {CONF_HOST: "stopper.local", CONF_WEBHOOK_ID: new_id}
        )
    assert result["type"] is FlowResultType.ABORT
    runtime.async_register_webhook.assert_called_once_with(new_id)
    runtime.async_unregister_webhook.assert_called_once_with(WEBHOOK_ID)
    assert entry.data[CONF_WEBHOOK_ID] == new_id
    runtime.async_configure_and_test.assert_awaited_with(
        f"http://home/api/webhook/{new_id}", api
    )

    runtime.async_configure_and_test.side_effect = RuntimeError
    with (
        patch(
            "custom_components.advanced_shot_stopper.config_flow.ShotStopperApi",
            return_value=api,
        ),
        patch(
            "custom_components.advanced_shot_stopper.config_flow.webhook_url",
            side_effect=lambda _hass, value: f"http://home/api/webhook/{value}",
        ),
    ):
        result = await hass.config_entries.flow.async_init(
            DOMAIN,
            context={
                "source": config_entries.SOURCE_RECONFIGURE,
                "entry_id": entry.entry_id,
            },
            data={CONF_HOST: "stopper.local", CONF_WEBHOOK_ID: WEBHOOK_ID},
        )
    assert result["type"] is FlowResultType.FORM


async def test_reconfigure_double_failure_retains_candidate_receiver(hass) -> None:
    entry = config_entry()
    entry.add_to_hass(hass)
    old_api = api_mock()
    candidate_api = api_mock()
    candidate_api.async_apply_webhook_config.side_effect = ProtocolError("rollback")
    runtime = MagicMock()
    runtime.api = old_api
    runtime.coordinator.api = old_api
    runtime.async_configure_and_test = AsyncMock(side_effect=RuntimeError("test"))
    entry.runtime_data = runtime
    candidate = "candidate_webhook_id_with_32_chars_1234"
    with (
        patch(
            "custom_components.advanced_shot_stopper.config_flow.ShotStopperApi",
            return_value=candidate_api,
        ),
        patch(
            "custom_components.advanced_shot_stopper.config_flow.webhook_url",
            side_effect=lambda _hass, value: f"http://home/{value}",
        ),
        patch(
            "custom_components.advanced_shot_stopper.config_flow.async_create_webhook_repair"
        ) as repair,
    ):
        result = await hass.config_entries.flow.async_init(
            DOMAIN,
            context={
                "source": config_entries.SOURCE_RECONFIGURE,
                "entry_id": entry.entry_id,
            },
            data={CONF_HOST: "new.local", CONF_WEBHOOK_ID: candidate},
        )
    assert result["type"] is FlowResultType.FORM
    runtime.async_register_webhook.assert_called_once_with(candidate)
    runtime.async_unregister_webhook.assert_not_called()
    assert runtime.api is old_api
    assert runtime.coordinator.api is old_api
    repair.assert_called_once_with(hass, entry.entry_id)


async def test_reconfigure_collision_preserves_runtime_and_identity_abort(hass) -> None:
    entry = config_entry()
    entry.add_to_hass(hass)
    api = api_mock()
    runtime = MagicMock()
    runtime.api = runtime.coordinator.api = api
    runtime.async_register_webhook.side_effect = ValueError("registered")
    entry.runtime_data = runtime
    candidate = "candidate_webhook_id_with_32_chars_1234"
    with (
        patch(
            "custom_components.advanced_shot_stopper.config_flow.ShotStopperApi",
            return_value=api,
        ),
        patch(
            "custom_components.advanced_shot_stopper.config_flow.webhook_url",
            side_effect=lambda _hass, value: f"http://home/{value}",
        ),
    ):
        result = await hass.config_entries.flow.async_init(
            DOMAIN,
            context={"source": config_entries.SOURCE_RECONFIGURE, "entry_id": entry.entry_id},
            data={CONF_HOST: "new.local", CONF_WEBHOOK_ID: candidate},
        )
    assert result["errors"] == {"base": "invalid_webhook_id"}
    assert runtime.api is api

    different = coordinator_data().snapshot.__class__.from_dict(
        {**fixture("integration_snapshot.json"), "deviceId": "11:22:33:44:55:66"}
    )
    api.async_snapshot.return_value = different
    runtime.async_register_webhook.side_effect = None
    with (
        patch("custom_components.advanced_shot_stopper.config_flow.ShotStopperApi", return_value=api),
        patch("custom_components.advanced_shot_stopper.config_flow.webhook_url", side_effect=lambda _hass, value: f"http://home/{value}"),
    ):
        result = await hass.config_entries.flow.async_init(
            DOMAIN,
            context={"source": config_entries.SOURCE_RECONFIGURE, "entry_id": entry.entry_id},
            data={CONF_HOST: "new.local", CONF_WEBHOOK_ID: candidate},
        )
    assert result["type"] is FlowResultType.ABORT
    assert result["reason"] == "unique_id_mismatch"


@pytest.mark.parametrize(
    "candidate", ["short", "contains spaces and is still invalid________"]
)
async def test_reconfigure_rejects_weak_ids(hass, candidate: str) -> None:
    """Only bounded URL-safe webhook IDs are accepted."""
    entry = config_entry()
    entry.add_to_hass(hass)
    entry.runtime_data = MagicMock()
    with patch(
        "custom_components.advanced_shot_stopper.config_flow.webhook_url",
        side_effect=lambda _hass, value: f"http://home/{value}",
    ):
        result = await hass.config_entries.flow.async_init(
            DOMAIN,
            context={
                "source": config_entries.SOURCE_RECONFIGURE,
                "entry_id": entry.entry_id,
            },
        )
        result = await hass.config_entries.flow.async_configure(
            result["flow_id"], {CONF_HOST: "stopper.local", CONF_WEBHOOK_ID: candidate}
        )
    assert result["errors"] == {"base": "invalid_webhook_id"}


async def test_reconfigure_maps_protocol_error(hass) -> None:
    """Malformed controller responses are not reported as invalid webhook IDs."""
    entry = config_entry()
    entry.add_to_hass(hass)
    entry.runtime_data = MagicMock()
    api = api_mock()
    api.async_snapshot.side_effect = ProtocolError("bad response")
    with (
        patch(
            "custom_components.advanced_shot_stopper.config_flow.ShotStopperApi",
            return_value=api,
        ),
        patch(
            "custom_components.advanced_shot_stopper.config_flow.webhook_url",
            side_effect=lambda _hass, value: f"http://home/{value}",
        ),
    ):
        result = await hass.config_entries.flow.async_init(
            DOMAIN,
            context={
                "source": config_entries.SOURCE_RECONFIGURE,
                "entry_id": entry.entry_id,
            },
            data={CONF_HOST: "stopper.local", CONF_WEBHOOK_ID: WEBHOOK_ID},
        )
    assert result["errors"] == {"base": "reconfigure_failed"}
