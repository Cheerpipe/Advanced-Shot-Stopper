idf_component_register(
  SRCS "ShotStopperBleRuntime.cpp" "ShotStopperBleArbiter.cpp"
  INCLUDE_DIRS "."
  REQUIRES bt esp_timer heap)
