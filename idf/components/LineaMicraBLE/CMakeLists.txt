set(LINEA_MICRA_BLE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../libraries/LineaMicraBLE")

set(LINEA_MICRA_BLE_SRCS)
if(SHOT_STOPPER_MACHINE_INTEGRATION STREQUAL "linea_micra_ble")
  list(APPEND LINEA_MICRA_BLE_SRCS
    "${LINEA_MICRA_BLE_DIR}/src/LineaMicraBLE.cpp"
    "${LINEA_MICRA_BLE_DIR}/src/LineaMicraProtocol.cpp")
endif()

idf_component_register(
  SRCS ${LINEA_MICRA_BLE_SRCS}
  INCLUDE_DIRS "${LINEA_MICRA_BLE_DIR}/src"
  REQUIRES ShotStopperBleRuntime
  PRIV_REQUIRES bt espressif__cjson)
