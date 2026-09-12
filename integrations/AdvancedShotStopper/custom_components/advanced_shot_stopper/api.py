"""Asynchronous client for the controller integration API."""

from __future__ import annotations

import asyncio
from typing import Any
from urllib.parse import urlsplit

from aiohttp import ClientError, ClientResponse, ClientSession, ClientTimeout

from .models import DeviceSnapshot, PresetState, ProtocolError


class ApiError(Exception):
    """Base controller API error."""


class CannotConnect(ApiError):
    """The controller could not be reached."""


class IncompatibleApi(ApiError):
    """The controller API is not compatible."""


class RequestRejected(ApiError):
    """A mutation was rejected with a stable controller code."""

    def __init__(self, code: str, status: int) -> None:
        super().__init__(code)
        self.code = code
        self.status = status


def normalize_host(value: str) -> str:
    """Accept only a host/IP, with an optional port."""
    candidate = value.strip()
    parsed = urlsplit(f"//{candidate}")
    if (
        not candidate
        or len(candidate) > 253
        or parsed.scheme
        or parsed.username
        or parsed.password
        or not parsed.hostname
        or parsed.path
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("invalid host")
    try:
        port = parsed.port
    except ValueError as err:
        raise ValueError("invalid port") from err
    return (
        f"[{parsed.hostname}]:{port}"
        if ":" in parsed.hostname and port
        else (parsed.hostname if port is None else f"{parsed.hostname}:{port}")
    )


class ShotStopperApi:
    """Bounded, session-injected local REST client."""

    def __init__(self, session: ClientSession, host: str) -> None:
        self._session = session
        self.host = normalize_host(host)
        self._base = f"http://{self.host}/api/v1/integration"

    async def _json(
        self,
        method: str,
        path: str = "",
        *,
        body: dict[str, Any] | None = None,
    ) -> tuple[int, dict[str, Any]]:
        headers = {"Accept": "application/json"}
        try:
            async with self._session.request(
                method,
                self._base + path,
                json=body,
                headers=headers,
                timeout=ClientTimeout(total=10),
            ) as response:
                return await self._decode(response)
        except (TimeoutError, ClientError) as err:
            raise CannotConnect from err

    @staticmethod
    async def _decode(response: ClientResponse) -> tuple[int, dict[str, Any]]:
        try:
            payload = await response.json(content_type=None)
        except (ValueError, UnicodeDecodeError) as err:
            raise ProtocolError("controller response is not JSON") from err
        if not isinstance(payload, dict):
            raise ProtocolError("controller response must be an object")
        if response.status >= 400:
            code = payload.get("error")
            raise RequestRejected(
                code if isinstance(code, str) else "UNKNOWN", response.status
            )
        return response.status, payload

    async def async_snapshot(self) -> DeviceSnapshot:
        _, payload = await self._json("GET")
        try:
            return DeviceSnapshot.from_dict(payload)
        except ProtocolError as err:
            if "API version" in str(err) or "capability" in str(err):
                raise IncompatibleApi from err
            raise

    async def async_presets(self) -> PresetState:
        _, payload = await self._json("GET", "/presets")
        return PresetState.from_dict(payload)

    async def async_webhook_config(self) -> dict[str, Any]:
        _, payload = await self._json("GET", "/webhook")
        return payload

    async def async_wait_request(self, request_id: int, timeout: float = 10) -> None:
        async with asyncio.timeout(timeout):
            while True:
                _, payload = await self._json("GET", "/request")
                observed = payload.get("requestId")
                state = payload.get("state")
                if observed == request_id and state == "PERSISTED":
                    return
                if observed == request_id and state in ("FAILED", "CANCELED"):
                    raise RequestRejected("PERSISTENCE_FAILED", 500)
                if isinstance(observed, int) and observed > request_id:
                    raise RequestRejected("REQUEST_SUPERSEDED", 409)
                await asyncio.sleep(0.2)

    async def _mutation(self, method: str, path: str, body: dict[str, Any]) -> None:
        _, payload = await self._json(method, path, body=body)
        request_id = payload.get("requestId")
        if isinstance(request_id, bool) or not isinstance(request_id, int):
            raise ProtocolError("mutation did not return requestId")
        await self.async_wait_request(request_id)

    async def async_configure_webhook(self, url: str, enabled: bool = True) -> None:
        await self.async_apply_webhook_config(
            {
                "enabled": enabled,
                "url": url if enabled else "",
                "brewState": enabled,
                "firstDrop": enabled,
                "end": enabled,
                "presetChanges": enabled,
                "deferDuringShot": False,
            }
        )

    async def async_apply_webhook_config(self, config: dict[str, Any]) -> None:
        """Apply a complete webhook snapshot through the durable command path."""
        fields = (
            "enabled",
            "url",
            "brewState",
            "firstDrop",
            "end",
            "presetChanges",
            "deferDuringShot",
        )
        await self._mutation("PUT", "/webhook", {key: config[key] for key in fields})

    async def async_test_webhook(self, correlation_id: str) -> None:
        await self._json(
            "POST",
            "/webhook/test",
            body={"correlationId": correlation_id},
        )

    async def async_select_preset(self, preset_id: int) -> None:
        await self._mutation("PUT", "/presets/active", {"id": preset_id})

    async def async_set_quick_setting(
        self, field: str, value: bool | str, base_revision: int
    ) -> None:
        """Persist one Home Quick Setting and wait for controller confirmation."""
        await self._mutation(
            "PUT", "/quick-settings", {"baseRevision": base_revision, field: value}
        )

    async def async_restart(self) -> None:
        """Queue one safe restart without polling the rebooting controller."""
        status, payload = await self._json("POST", "/restart", body={})
        request_id = payload.get("requestId")
        if status != 202 or isinstance(request_id, bool) or not isinstance(
            request_id, int
        ):
            raise ProtocolError("restart did not return requestId")
