"""Repairs support for Advanced Shot Stopper."""

from homeassistant.core import HomeAssistant
from homeassistant.helpers import issue_registry as ir

from .const import DOMAIN


def async_create_webhook_repair(hass: HomeAssistant, entry_id: str) -> None:
    """Report the rare state where remote webhook rollback also failed."""
    ir.async_create_issue(
        hass,
        DOMAIN,
        f"webhook_rollback_{entry_id}",
        is_fixable=False,
        severity=ir.IssueSeverity.ERROR,
        translation_key="webhook_rollback_failed",
    )
