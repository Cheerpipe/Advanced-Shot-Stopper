# Application firmware sources live in ../../../src. This component is
# their ESP-IDF registration.
set(SHOT_STOPPER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../src")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

set(SHOT_STOPPER_SRCS
  "${SHOT_STOPPER_DIR}/openBrew.cpp"
  "${SHOT_STOPPER_DIR}/OpenBrewTaskMutex.cpp"
  "${SHOT_STOPPER_DIR}/OpenBrewResetGuard.cpp"
  "${SHOT_STOPPER_DIR}/OpenBrewCrashArchive.cpp"
  "${SHOT_STOPPER_DIR}/OpenBrewAuditStore.cpp"
  "${SHOT_STOPPER_DIR}/OpenBrewPowerManagement.cpp"
  "${SHOT_STOPPER_DIR}/OpenBrewScaleWorker.cpp"
  "${SHOT_STOPPER_DIR}/OpenBrewNetwork.cpp"
  "${SHOT_STOPPER_DIR}/OpenBrewOta.cpp"
  "${SHOT_STOPPER_DIR}/OpenBrewWebhook.cpp")

set(SHOT_STOPPER_REQUIRES
  arduino-esp32
  EspressoScaleBLE
  OpenBrewBleRuntime)
if(SHOT_STOPPER_MACHINE_INTEGRATION STREQUAL "linea_micra_cloud")
  list(APPEND SHOT_STOPPER_SRCS
    "${SHOT_STOPPER_DIR}/machine/OpenBrewLineaMicraIntegration.cpp"
    "${SHOT_STOPPER_DIR}/machine/OpenBrewMicraService.cpp"
    "${SHOT_STOPPER_DIR}/machine/OpenBrewMicraObservation.cpp")
else()
  list(APPEND SHOT_STOPPER_SRCS
    "${SHOT_STOPPER_DIR}/machine/OpenBrewMachineIntegration.cpp")
endif()

idf_component_register(
  SRCS ${SHOT_STOPPER_SRCS}
  INCLUDE_DIRS
    "${CMAKE_BINARY_DIR}/generated"
    "${SHOT_STOPPER_DIR}"
  REQUIRES
    ${SHOT_STOPPER_REQUIRES}
  PRIV_REQUIRES
    espressif__esp_websocket_client
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
    espcoredump
    esp_hw_support
    log
    heap
    soc
    # WiFiGeneric.h includes this even when CONFIG_ARDUINO_SELECTIVE_WiFiProv
    # is unset (selective compilation). main used to get it via auto-REQUIRES.
    espressif__network_provisioning
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
