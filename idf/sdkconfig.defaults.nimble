# Production native NimBLE host. The application acts as a central for scales;
# no Companion profile is registered or advertised in this build.
# CONFIG_BT_CONTROLLER_ONLY is not set
CONFIG_BT_NIMBLE_ENABLED=y

CONFIG_BT_NIMBLE_ROLE_CENTRAL=y
CONFIG_BT_NIMBLE_ROLE_OBSERVER=y
CONFIG_BT_NIMBLE_GATT_CLIENT=y
# Retain the qualified role/capacity settings until target resource measurements
# establish that reducing them preserves NimBLE initialization and recovery.
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_GATT_SERVER=y

# One concurrent link: the scale worker connects a single scale by design.
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_CTRL_BLE_MAX_ACT=3
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=96
CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES=4
CONFIG_BT_NIMBLE_GATT_MAX_PROCS=2
CONFIG_BT_NIMBLE_MAX_CCCDS=4
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096

CONFIG_BT_NIMBLE_MEM_OPTIMIZATION=y
CONFIG_BT_NIMBLE_STATIC_TO_DYNAMIC=y
# CONFIG_BT_NIMBLE_MEMPOOL_RUNTIME_ALLOC is not set
CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y
CONFIG_BT_NIMBLE_HOST_QUEUE_CONG_CHECK=y
CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT=y
CONFIG_BT_NIMBLE_MAX_CONN_REATTEMPT=3

# CONFIG_BT_NIMBLE_GATT_CACHING is not set
# CONFIG_BT_NIMBLE_SECURITY_ENABLE is not set
# CONFIG_BT_NIMBLE_HS_PVCY is not set
# CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT is not set
# CONFIG_BT_NIMBLE_MESH is not set

# Shot Stopper registers only the standard GAP/GATT services. ESP-IDF enables
# these unrelated services by default; keep them out of production.
# CONFIG_BT_NIMBLE_PROX_SERVICE is not set
# CONFIG_BT_NIMBLE_ANS_SERVICE is not set
# CONFIG_BT_NIMBLE_CTS_SERVICE is not set
# CONFIG_BT_NIMBLE_HTP_SERVICE is not set
# CONFIG_BT_NIMBLE_IPSS_SERVICE is not set
# CONFIG_BT_NIMBLE_TPS_SERVICE is not set
# CONFIG_BT_NIMBLE_IAS_SERVICE is not set
# CONFIG_BT_NIMBLE_LLS_SERVICE is not set
# CONFIG_BT_NIMBLE_SPS_SERVICE is not set
# CONFIG_BT_NIMBLE_HR_SERVICE is not set
# CONFIG_BT_NIMBLE_BAS_SERVICE is not set
# CONFIG_BT_NIMBLE_DIS_SERVICE is not set

# No Direct Test Mode, signed-write counter or characteristic presentation /
# aggregate descriptors are used. ATT MTU reconfiguration stays enabled for
# scale GATT interoperability under the qualified production profile.
# CONFIG_BT_NIMBLE_DTM_MODE_TEST is not set
# CONFIG_BT_NIMBLE_SM_SIGN_CNT is not set
# CONFIG_BT_NIMBLE_CPFD_CAFD is not set
