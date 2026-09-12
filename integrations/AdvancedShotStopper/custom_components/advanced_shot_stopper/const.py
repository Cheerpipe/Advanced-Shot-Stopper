"""Constants for Advanced Shot Stopper."""

import re
from datetime import timedelta

DOMAIN = "advanced_shot_stopper"
PLATFORMS = ["sensor", "select"]
CONF_DEVICE_ID = "device_id"
CONF_WEBHOOK_ID = "webhook_id"
API_VERSION = 1
REQUIRED_CAPABILITIES = frozenset({"webhook_v1", "preset_select_v1"})
UPDATE_INTERVAL = timedelta(minutes=5)
WEBHOOK_TEST_TIMEOUT = 10
WEBHOOK_ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]{32,128}$")
MANUFACTURER = "Advanced Shot Stopper"
MODEL = "Advanced Shot Stopper"
STORE_VERSION = 1
STORE_KEY_PREFIX = f"{DOMAIN}.shots"

SHOT_TYPES = ("auto", "timer_only", "manual")
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
