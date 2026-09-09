find_package(Threads REQUIRED)

execute_process(
  COMMAND "${CMAKE_SOURCE_DIR}/scripts/gen_version.sh"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY)

set(SHOTSTOPPER_SANITIZER "" CACHE STRING "Host sanitizer")
set(common_options -Wall -Wextra -Werror -pedantic)
if(SHOTSTOPPER_SANITIZER)
  list(APPEND common_options -fno-omit-frame-pointer -fsanitize=${SHOTSTOPPER_SANITIZER})
  add_link_options(-fno-omit-frame-pointer -fsanitize=${SHOTSTOPPER_SANITIZER})
endif()

function(host_test name)
  cmake_parse_arguments(ARG "" "LABEL;SOURCE" "SOURCES;DEFINITIONS;ARGS" ${ARGN})
  if(NOT ARG_SOURCE)
    set(ARG_SOURCE ${name}.cpp)
  endif()
  add_executable(${name} ${ARG_SOURCE} ${ARG_SOURCES})
  target_compile_features(${name} PRIVATE cxx_std_17)
  target_compile_options(${name} PRIVATE ${common_options})
  target_compile_definitions(${name} PRIVATE ${ARG_DEFINITIONS})
  target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(${name} PRIVATE Threads::Threads)
  add_test(NAME ${name} COMMAND ${name} ${ARG_ARGS})
  set_tests_properties(${name} PROPERTIES LABELS "${ARG_LABEL}")
endfunction()

if(SHOTSTOPPER_SANITIZER STREQUAL "thread")
  foreach(case M09 R25c F03 F04 F17 IT10 IT28)
    host_test(shot_stopper_tsan_${case}
      SOURCE shot_stopper_host_test.cpp ARGS ${case} LABEL "host;concurrency")
    set_target_properties(shot_stopper_tsan_${case} PROPERTIES
      OUTPUT_NAME shot_stopper_tsan_${case})
  endforeach()
  host_test(ota_state_concurrency_host_test LABEL "host;ota;concurrency")
else()
  host_test(shot_stopper_host_test LABEL "host;control")
  host_test(persistence_host_test LABEL "host;persistence")
  host_test(bbw_cutoff_host_test LABEL "host;control;persistence")
  host_test(safety_external_host_test LABEL "host;safety")
  host_test(remote_policy_host_test LABEL "host;safety")
  host_test(remote_lockdown_host_test LABEL "host;safety;web")
  host_test(ota_image_host_test LABEL "host;ota")
  host_test(ota_transfer_host_test LABEL "host;ota")
  host_test(ota_state_concurrency_host_test LABEL "host;ota;concurrency")
  host_test(ble_companion_protocol_host_test LABEL "host;ble")
  host_test(webhook_error_host_test
    DEFINITIONS SHOT_STOPPER_HOST_TEST LABEL "host;network")
  host_test(webhook_worker_host_test LABEL "host;network")
  host_test(mdns_lifecycle_host_test LABEL "host;network")
  host_test(resource_owner_host_test LABEL "host;resources")
  host_test(reset_guard_instance_host_test
    SOURCES reset_guard_instance_a.cpp reset_guard_instance_b.cpp
    LABEL "host;safety")
  foreach(machine_type 1 2)
    set(target momentary_machine_type_${machine_type})
    add_executable(${target} momentary_machine_host_test.cpp)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE ${common_options})
    target_compile_definitions(${target} PRIVATE SHOT_STOPPER_MACHINE_TYPE=${machine_type})
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(${target} PRIVATE Threads::Threads)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES LABELS "host;control;safety")
  endforeach()

  set(ble_dir "${CMAKE_SOURCE_DIR}/libraries/EspressoScaleBLE")
  function(ble_test name)
    add_executable(${name} "${ble_dir}/tests/${name}.cpp" ${ARGN})
    target_compile_features(${name} PRIVATE cxx_std_11)
    target_compile_options(${name} PRIVATE ${common_options})
    target_include_directories(${name} PRIVATE "${ble_dir}/tests/stubs" "${ble_dir}/src")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS "host;ble")
  endfunction()
  ble_test(scale_ble_portable_test "${ble_dir}/src/ScaleBleStateMachine.cpp")
  ble_test(nimble_advertisement_test
    "${ble_dir}/src/ScaleProtocolCommon.cpp"
    "${ble_dir}/src/protocols/Registry.cpp"
    "${ble_dir}/src/protocols/Acaia.cpp"
    "${ble_dir}/src/protocols/GenericFf11.cpp"
    "${ble_dir}/src/protocols/Felicita.cpp"
    "${ble_dir}/src/protocols/Eclair.cpp"
    "${ble_dir}/src/protocols/Decent.cpp"
    "${ble_dir}/src/protocols/Difluid.cpp"
    "${ble_dir}/src/protocols/Myscale.cpp"
    "${ble_dir}/src/protocols/WeighMyBru.cpp"
    "${ble_dir}/src/protocols/Varia.cpp"
    "${ble_dir}/src/protocols/Eureka.cpp"
    "${ble_dir}/src/nimble/NimbleAdvertisement.cpp")
  ble_test(nimble_resilience_test "${ble_dir}/src/nimble/NimbleResilience.cpp")
endif()

set(ble_dir "${CMAKE_SOURCE_DIR}/libraries/EspressoScaleBLE")
file(GLOB scale_protocol_sources CONFIGURE_DEPENDS "${ble_dir}/src/protocols/*.cpp")
host_test(nimble_client_test
  SOURCE "${ble_dir}/tests/nimble_client_test.cpp"
  SOURCES "${ble_dir}/src/ScaleProtocolCommon.cpp"
    "${ble_dir}/src/nimble/NimbleAdvertisement.cpp"
    "${ble_dir}/src/nimble/NimbleResilience.cpp" ${scale_protocol_sources}
  DEFINITIONS ESPRESSO_SCALE_BLE_HOST_TEST LABEL "host;ble;concurrency")
target_include_directories(nimble_client_test PRIVATE "${ble_dir}/tests/stubs" "${ble_dir}/src")
set_tests_properties(nimble_client_test PROPERTIES LABELS "host;ble;concurrency")

find_path(CJSON_INCLUDE_DIR cJSON.h PATH_SUFFIXES cjson)
find_library(CJSON_LIBRARY cjson)
if(NOT CJSON_INCLUDE_DIR OR NOT CJSON_LIBRARY)
  message(FATAL_ERROR "libcjson is required for host tests")
endif()
host_test(json_arena_host_test LABEL "host;concurrency")
target_include_directories(json_arena_host_test PRIVATE "${CJSON_INCLUDE_DIR}")
target_link_libraries(json_arena_host_test PRIVATE "${CJSON_LIBRARY}")
