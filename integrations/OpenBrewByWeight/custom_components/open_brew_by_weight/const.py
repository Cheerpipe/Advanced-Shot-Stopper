"""Constants for Open Brew by Weight."""

import re

DOMAIN = "open_brew_by_weight"
PLATFORMS = ["sensor", "select", "switch", "button"]
CONF_DEVICE_ID = "device_id"
CONF_WEBHOOK_ID = "webhook_id"
API_VERSION = 1
REQUIRED_CAPABILITIES = frozenset(
    {
        "webhook_v1",
        "preset_select_v1",
        "quick_settings_v1",
        "restart_v1",
        "stored_shots_v1",
    }
)
RECOVERY_DELAYS = (5, 10, 20, 40, 60)
WEBHOOK_TEST_TIMEOUT = 10
WEBHOOK_ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]{32,128}$")
MANUFACTURER = "Open Brew by Weight"
MODEL = "Open Brew by Weight"
STORE_VERSION = 1
STORE_KEY_PREFIX = f"{DOMAIN}.shots"

SHOT_TYPES = ("auto", "timer_only", "manual")
ACTIVATION_TYPES = ("shot", "rinse", "other", "power_on")
STOP_DETAILS = (
    "normal_target",
    "extended_max_weight",
    "extended_min_time",
    "auto_to_manual",
    "slow_max_time",
    "slow_min_weight",
    "cup_removed",
    "activator",
    "web_stop",
    "web_heartbeat",
    "physical_override",
    "hard_limit",
    "wall_limit",
    "relay_safety",
    "weight_anomaly",
    "other",
)
