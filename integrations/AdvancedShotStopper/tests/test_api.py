"""Async client tests without network access."""

from __future__ import annotations

import json
from pathlib import Path
from unittest.mock import AsyncMock

import pytest

from custom_components.advanced_shot_stopper.api import (
    CannotConnect,
    IncompatibleApi,
    RequestRejected,
    ShotStopperApi,
    normalize_host,
)
from custom_components.advanced_shot_stopper.models import ProtocolError

FIXTURES = Path(__file__).parents[3] / "src" / "tests" / "fixtures"


class Response:
    """Minimal aiohttp response double."""

    def __init__(self, status: int, payload: dict) -> None:
        self.status = status
        self.json = AsyncMock(return_value=payload)


class Request:
    """Async context manager returned by a fake session."""

    def __init__(self, response: Response) -> None:
        self.response = response

    async def __aenter__(self) -> Response:
        return self.response

    async def __aexit__(self, *args) -> None:
        return None


class Session:
    """Finite response queue that records requests."""

    def __init__(self, *responses: Response) -> None:
        self.responses = list(responses)
        self.calls: list[tuple] = []

    def request(self, *args, **kwargs) -> Request:
        self.calls.append((args, kwargs))
        return Request(self.responses.pop(0))


def fixture(name: str) -> dict:
    return json.loads((FIXTURES / name).read_text())


@pytest.mark.parametrize("host", ["192.168.1.8", "stopper.local", "stopper.local:8080"])
def test_normalize_host(host: str) -> None:
    assert normalize_host(host) == host


@pytest.mark.parametrize(
    "host", ["", "http://stopper", "user@stopper", "stopper/path", "stopper:70000"]
)
def test_reject_host(host: str) -> None:
    with pytest.raises(ValueError):
        normalize_host(host)


async def test_reads_snapshot_and_presets() -> None:
    session = Session(
        Response(200, fixture("integration_snapshot.json")),
        Response(200, fixture("integration_presets.json")),
    )
    api = ShotStopperApi(session, "stopper.local")
    assert (await api.async_snapshot()).shot_state == "idle"
    assert (await api.async_presets()).active_id == 2


async def test_durable_mutation_is_open_on_the_local_api() -> None:
    session = Session(
        Response(202, {"apiVersion": 1, "requestId": 5}),
        Response(200, {"apiVersion": 1, "requestId": 5, "state": "PERSISTED"}),
    )
    api = ShotStopperApi(session, "stopper.local")
    await api.async_select_preset(2)
    assert session.calls[0][1]["headers"] == {"Accept": "application/json"}


async def test_maps_stable_errors() -> None:
    api = ShotStopperApi(
        Session(Response(409, {"error": "CONFIG_LOCKED_DURING_ACTIVE_CYCLE"})),
        "stopper",
    )
    with pytest.raises(RequestRejected) as raised:
        await api.async_select_preset(2)
    assert raised.value.code == "CONFIG_LOCKED_DURING_ACTIVE_CYCLE"


async def test_transport_and_response_validation() -> None:
    """Transport and JSON failures retain stable meanings."""

    class BrokenSession:
        def request(self, *args, **kwargs):
            raise TimeoutError

    with pytest.raises(CannotConnect):
        await ShotStopperApi(BrokenSession(), "stopper").async_snapshot()

    response = Response(200, {})
    response.json.side_effect = ValueError
    with pytest.raises(ProtocolError, match="not JSON"):
        await ShotStopperApi._decode(response)
    with pytest.raises(ProtocolError, match="object"):
        await ShotStopperApi._decode(Response(200, []))
    with pytest.raises(RequestRejected) as raised:
        await ShotStopperApi._decode(Response(500, {}))
    assert raised.value.code == "UNKNOWN"


async def test_incompatible_snapshot() -> None:
    """An incompatible version is rejected before config-entry creation."""
    incompatible = fixture("integration_snapshot.json")
    incompatible["apiVersion"] = 2
    with pytest.raises(IncompatibleApi):
        await ShotStopperApi(
            Session(Response(200, incompatible)), "stopper"
        ).async_snapshot()
    incompatible = fixture("integration_snapshot.json")
    incompatible["capabilities"] = ["webhook_v1"]
    with pytest.raises(IncompatibleApi):
        await ShotStopperApi(
            Session(Response(200, incompatible)), "stopper"
        ).async_snapshot()

async def test_request_polling_states(monkeypatch) -> None:
    """Durable request polling detects terminal failures."""
    monkeypatch.setattr("asyncio.sleep", AsyncMock())
    session = Session(
        Response(200, {"requestId": 7, "state": "QUEUED"}),
        Response(200, {"requestId": 7, "state": "PERSISTED"}),
    )
    await ShotStopperApi(session, "stopper").async_wait_request(7)

    for payload, code in (
        ({"requestId": 7, "state": "FAILED"}, "PERSISTENCE_FAILED"),
        ({"requestId": 8, "state": "PERSISTED"}, "REQUEST_SUPERSEDED"),
    ):
        with pytest.raises(RequestRejected) as raised:
            await ShotStopperApi(Session(Response(200, payload)), "stopper").async_wait_request(7)
        assert raised.value.code == code


async def test_all_mutation_shapes() -> None:
    """Every mutation uses the bounded open integration API shape."""
    persisted = Response(200, {"requestId": 9, "state": "PERSISTED"})
    session = Session(
        Response(202, {"requestId": 9}),
        persisted,
        Response(200, {}),
        Response(202, {"requestId": 10}),
        Response(200, {"requestId": 10, "state": "PERSISTED"}),
        Response(202, {"requestId": 11}),
        Response(200, {"requestId": 11, "state": "PERSISTED"}),
        Response(202, {"apiVersion": 1, "requestId": 12}),
    )
    api = ShotStopperApi(session, "stopper")
    await api.async_configure_webhook("http://home/webhook")
    await api.async_test_webhook("proof")
    await api.async_select_preset(2)
    await api.async_set_quick_setting("brewByWeight", False, 19)
    await api.async_restart()
    assert session.calls[0][0][:2] == (
        "PUT",
        "http://stopper/api/v1/integration/webhook",
    )
    assert session.calls[2][1]["json"] == {"correlationId": "proof"}
    assert session.calls[5][1]["json"] == {
        "baseRevision": 19,
        "brewByWeight": False,
    }
    assert session.calls[7][1]["json"] == {}

    with pytest.raises(ProtocolError, match="requestId"):
        await ShotStopperApi(
            Session(Response(202, {})), "stopper"
        ).async_select_preset(2)
    with pytest.raises(ProtocolError, match="restart"):
        await ShotStopperApi(
            Session(Response(200, {"requestId": True})), "stopper"
        ).async_restart()
