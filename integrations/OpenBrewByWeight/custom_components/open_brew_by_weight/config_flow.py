"""Configuration and reconfiguration flows."""

from __future__ import annotations

from typing import Any

import voluptuous as vol
from homeassistant import config_entries
from homeassistant.components import webhook
from homeassistant.const import CONF_HOST
from homeassistant.data_entry_flow import AbortFlow
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.service_info.zeroconf import ZeroconfServiceInfo

from .api import (
    ApiError,
    CannotConnect,
    IncompatibleApi,
    OpenBrewByWeightApi,
    normalize_host,
)
from .const import (
    CONF_DEVICE_ID,
    CONF_WEBHOOK_ID,
    DOMAIN,
    WEBHOOK_ID_PATTERN,
)
from .models import ProtocolError
from .repairs import async_create_webhook_repair
from .runtime import webhook_url

MDNS_SERVICE_SUFFIX = "._http._tcp.local."


class OpenBrewByWeightConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Configure a controller from its LAN address."""

    VERSION = 1
    MINOR_VERSION = 1

    def __init__(self) -> None:
        self._pending: dict[str, Any] | None = None
        self._discovered_host: str | None = None
        self._discovered_name: str | None = None

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        """Accept exactly one user field: the controller host."""
        if user_input is None:
            return self.async_show_form(
                step_id="user", data_schema=vol.Schema({vol.Required(CONF_HOST): str})
            )
        errors: dict[str, str] = {}
        try:
            host = normalize_host(user_input[CONF_HOST])
            return await self._prepare_entry(host)
        except CannotConnect:
            errors["base"] = "cannot_connect"
        except IncompatibleApi:
            errors["base"] = "incompatible_api"
        except (ApiError, ProtocolError):
            errors["base"] = "cannot_connect"
        except ValueError:
            errors["base"] = "invalid_host"
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {vol.Required(CONF_HOST, default=user_input[CONF_HOST]): str}
            ),
            errors=errors,
        )

    async def async_step_zeroconf(
        self, discovery_info: ZeroconfServiceInfo
    ) -> config_entries.ConfigFlowResult:
        """Identify a controller announced over mDNS and ask for confirmation."""
        host = discovery_info.host
        if discovery_info.port not in (None, 80):
            host = f"{host}:{discovery_info.port}"
        name = discovery_info.name
        if name.endswith(MDNS_SERVICE_SUFFIX):
            name = name[: -len(MDNS_SERVICE_SUFFIX)]
        try:
            snapshot = await OpenBrewByWeightApi(
                async_get_clientsession(self.hass), host
            ).async_snapshot()
        except IncompatibleApi:
            return self.async_abort(reason="incompatible_api")
        except (ApiError, ProtocolError):
            return self.async_abort(reason="cannot_connect")
        await self.async_set_unique_id(snapshot.device_id)
        self._abort_if_unique_id_configured(updates={CONF_HOST: host})
        self._discovered_host = host
        self._discovered_name = name
        return await self.async_step_confirm()

    async def async_step_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        """Confirm adding the discovered controller."""
        assert self._discovered_host is not None
        if user_input is None:
            return self.async_show_form(
                step_id="confirm",
                description_placeholders={"name": self._discovered_name or ""},
            )
        errors: dict[str, str] = {}
        try:
            return await self._prepare_entry(self._discovered_host)
        except IncompatibleApi:
            errors["base"] = "incompatible_api"
        except (ApiError, ProtocolError):
            errors["base"] = "cannot_connect"
        return self.async_show_form(
            step_id="confirm",
            errors=errors,
            description_placeholders={"name": self._discovered_name or ""},
        )

    async def _prepare_entry(
        self, host: str
    ) -> config_entries.ConfigFlowResult:
        """Identify the controller and stage the webhook configuration."""
        api = OpenBrewByWeightApi(async_get_clientsession(self.hass), host)
        snapshot = await api.async_snapshot()
        await self.async_set_unique_id(snapshot.device_id)
        self._abort_if_unique_id_configured()
        webhook_id = webhook.async_generate_id()
        callback = webhook_url(self.hass, webhook_id)
        existing = await api.async_webhook_config()
        self._pending = {
            CONF_HOST: host,
            CONF_DEVICE_ID: snapshot.device_id,
            CONF_WEBHOOK_ID: webhook_id,
            "callback": callback,
            "takeover": bool(
                existing.get("enabled") and existing.get("url") != callback
            ),
        }
        if self._pending["takeover"]:
            return await self.async_step_takeover()
        return self._create_pending_entry()

    async def async_step_takeover(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        """Require explicit consent before replacing another callback."""
        if user_input is None:
            return self.async_show_form(
                step_id="takeover",
                data_schema=vol.Schema({vol.Required("confirm", default=False): bool}),
            )
        if not user_input["confirm"]:
            return self.async_abort(reason="takeover_rejected")
        return self._create_pending_entry()

    def _create_pending_entry(self) -> config_entries.ConfigFlowResult:
        assert self._pending is not None
        data = {
            key: self._pending[key]
            for key in (
                CONF_HOST,
                CONF_DEVICE_ID,
                CONF_WEBHOOK_ID,
            )
        }
        return self.async_create_entry(title="Open Brew by Weight", data=data)

    async def async_step_reconfigure(
        self, user_input: dict[str, Any] | None = None
    ) -> config_entries.ConfigFlowResult:
        """Rotate/reapply the callback as one verified transaction."""
        entry = self._get_reconfigure_entry()
        current_id = entry.data[CONF_WEBHOOK_ID]
        if user_input is None:
            return self.async_show_form(
                step_id="reconfigure",
                data_schema=vol.Schema(
                    {
                        vol.Required(CONF_HOST, default=entry.data[CONF_HOST]): str,
                        vol.Required(CONF_WEBHOOK_ID, default=current_id): str,
                    }
                ),
                description_placeholders={
                    "webhook_url": webhook_url(self.hass, current_id)
                },
            )
        host = user_input[CONF_HOST]
        candidate_id = user_input[CONF_WEBHOOK_ID]
        errors: dict[str, str] = {}
        try:
            host = normalize_host(host)
            if not WEBHOOK_ID_PATTERN.fullmatch(candidate_id):
                raise ValueError("weak webhook ID")
            if any(
                other.entry_id != entry.entry_id
                and other.data.get(CONF_WEBHOOK_ID) == candidate_id
                for other in self._async_current_entries()
            ):
                raise ValueError("webhook ID collision")
            runtime = entry.runtime_data
            api = OpenBrewByWeightApi(async_get_clientsession(self.hass), host)
            snapshot = await api.async_snapshot()
            await self.async_set_unique_id(snapshot.device_id)
            self._abort_if_unique_id_mismatch()
            candidate_url = webhook_url(self.hass, candidate_id)
            previous = await api.async_webhook_config()
            registered_candidate = False
            try:
                if candidate_id != current_id:
                    runtime.async_register_webhook(candidate_id)
                    registered_candidate = True
                await runtime.async_configure_and_test(candidate_url, api)
            except Exception:
                try:
                    await api.async_apply_webhook_config(previous)
                except (ApiError, ProtocolError, TimeoutError):
                    async_create_webhook_repair(self.hass, entry.entry_id)
                else:
                    if registered_candidate:
                        runtime.async_unregister_webhook(candidate_id)
                raise
            runtime.api = runtime.coordinator.api = api
            if candidate_id != current_id:
                runtime.async_unregister_webhook(current_id)
            runtime.webhook_id, runtime.webhook_url = candidate_id, candidate_url
            return self.async_update_reload_and_abort(
                entry,
                data_updates={CONF_HOST: host, CONF_WEBHOOK_ID: candidate_id},
            )
        except CannotConnect:
            errors["base"] = "cannot_connect"
        except (ApiError, ProtocolError):
            errors["base"] = "reconfigure_failed"
        except ValueError:
            errors["base"] = "invalid_webhook_id"
        except AbortFlow:
            raise
        except Exception:
            errors["base"] = "reconfigure_failed"
        return self.async_show_form(
            step_id="reconfigure",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_HOST, default=host): str,
                    vol.Required(CONF_WEBHOOK_ID, default=candidate_id): str,
                }
            ),
            errors=errors,
            description_placeholders={
                "webhook_url": webhook_url(
                    self.hass,
                    candidate_id
                    if WEBHOOK_ID_PATTERN.fullmatch(candidate_id)
                    else current_id,
                )
            },
        )
