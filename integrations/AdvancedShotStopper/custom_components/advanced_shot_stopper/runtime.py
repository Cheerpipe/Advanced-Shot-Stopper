"""Config-entry runtime data and webhook transactions."""

from __future__ import annotations

import asyncio
import secrets
from dataclasses import dataclass, field
from urllib.parse import urlsplit

from aiohttp.web import Request, Response
from aiohttp.web_exceptions import HTTPRequestEntityTooLarge
from homeassistant.components import webhook
from homeassistant.core import HomeAssistant

from .api import ShotStopperApi
from .const import DOMAIN, WEBHOOK_TEST_TIMEOUT
from .coordinator import ShotStopperCoordinator


def webhook_url(hass: HomeAssistant, webhook_id: str) -> str:
    """Generate the controller-reachable internal HTTP callback URL."""
    url = webhook.async_generate_url(
        hass, webhook_id, allow_external=False, allow_ip=True, prefer_external=False
    )
    parsed = urlsplit(url)
    if (
        parsed.scheme != "http"
        or not parsed.hostname
        or parsed.username
        or parsed.password
    ):
        raise ValueError("unusable callback URL")
    return url


@dataclass(slots=True)
class ShotStopperRuntimeData:
    """Objects owned by one loaded config entry."""

    hass: HomeAssistant
    api: ShotStopperApi
    coordinator: ShotStopperCoordinator
    webhook_id: str
    webhook_url: str
    registered_webhook_ids: set[str] = field(default_factory=set)

    async def async_configure_and_test(
        self, url: str, api: ShotStopperApi | None = None
    ) -> None:
        """Idempotently configure the callback and prove delivery."""
        correlation = secrets.token_urlsafe(24)
        waiter = self.coordinator.expect_test(correlation)
        try:
            client = api or self.api
            await client.async_configure_webhook(url)
            await client.async_test_webhook(correlation)
            await asyncio.wait_for(waiter, WEBHOOK_TEST_TIMEOUT)
        finally:
            self.coordinator.cancel_test(correlation)

    def async_register_webhook(self, webhook_id: str) -> None:
        """Register one local-only POST receiver."""
        if webhook_id in self.registered_webhook_ids:
            return
        webhook.async_register(
            self.hass,
            DOMAIN,
            "Advanced Shot Stopper",
            webhook_id,
            self.async_handle_webhook,
            local_only=True,
            allowed_methods=["POST"],
        )
        self.registered_webhook_ids.add(webhook_id)

    async def async_handle_webhook(
        self, hass: HomeAssistant, webhook_id: str, request: Request
    ) -> Response:
        """Validate a bounded payload before touching coordinator state."""
        from .models import MAX_WEBHOOK_BYTES, ProtocolError, WebhookEvent

        if request.content_type != "application/json":
            return Response(status=400)
        if (
            request.content_length is not None
            and request.content_length > MAX_WEBHOOK_BYTES
        ):
            return Response(status=400)
        try:
            payload = await request.content.read(MAX_WEBHOOK_BYTES + 1)
            if len(payload) > MAX_WEBHOOK_BYTES:
                return Response(status=400)
            event = WebhookEvent.from_bytes(payload)
            await self.coordinator.async_process_webhook(event)
        except PermissionError:
            return Response(status=403)
        except (ProtocolError, HTTPRequestEntityTooLarge):
            return Response(status=400)
        return Response(status=204)

    def async_unregister_webhook(self, webhook_id: str | None = None) -> None:
        """Unregister a receiver without changing the controller."""
        selected = (
            (webhook_id,)
            if webhook_id is not None
            else tuple(self.registered_webhook_ids or {self.webhook_id})
        )
        for item in selected:
            webhook.async_unregister(self.hass, item)
            self.registered_webhook_ids.discard(item)
