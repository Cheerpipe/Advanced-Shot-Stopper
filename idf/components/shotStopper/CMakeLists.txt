# Application firmware sources live in ../../../src. This component is
# their ESP-IDF registration.
set(SHOT_STOPPER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../src")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

set(SHOT_STOPPER_SRCS
  "${SHOT_STOPPER_DIR}/shotStopper.cpp"
  "${SHOT_STOPPER_DIR}/ShotStopperResetGuard.cpp"
  "${SHOT_STOPPER_DIR}/ShotStopperPowerManagement.cpp"
  "${SHOT_STOPPER_DIR}/ShotStopperScaleWorker.cpp"
  "${SHOT_STOPPER_DIR}/ShotStopperNetwork.cpp"
  "${SHOT_STOPPER_DIR}/ShotStopperOta.cpp"
  "${SHOT_STOPPER_DIR}/ShotStopperWebhook.cpp")
list(APPEND SHOT_STOPPER_SRCS
  "${SHOT_STOPPER_DIR}/machine/ShotStopperMachineIntegration.cpp")

set(SHOT_STOPPER_REQUIRES
  arduino-esp32
  EspressoScaleBLE
  ShotStopperBleRuntime
  LineaMicraBLE)
if(SHOT_STOPPER_MACHINE_INTEGRATION STREQUAL "linea_micra_ble")
  list(APPEND SHOT_STOPPER_SRCS
    "${SHOT_STOPPER_DIR}/machine/ShotStopperMicraService.cpp")
endif()

idf_component_register(
  SRCS ${SHOT_STOPPER_SRCS}
  INCLUDE_DIRS
    "${CMAKE_BINARY_DIR}/generated"
    "${SHOT_STOPPER_DIR}"
  REQUIRES
    ${SHOT_STOPPER_REQUIRES}
  PRIV_REQUIRES
    espressif__cjson
    mbedtls
    nvs_flash
    esp_wifi
    esp_timer
    esp_pm
    esp_system
    esp_driver_gptimer
    bt
    app_update
    esp_http_server
    esp_http_client
    esp_partition
    esp_hw_support
    log
    heap
    soc
    # WiFiGeneric.h includes this even when CONFIG_ARDUINO_SELECTIVE_WiFiProv
    # is unset (selective compilation). main used to get it via auto-REQUIRES.
    espressif__network_provisioning
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
