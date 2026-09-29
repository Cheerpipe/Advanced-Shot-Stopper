# Empty stand-in for espressif/esp-modbus. arduino-esp32 declares esp-modbus
# as a public dependency although this firmware never uses Modbus, and the real
# component's TCP port compiles unguarded IPv6 types that force
# CONFIG_LWIP_IPV6=y. The override in main/idf_component.yml points here so
# neither Modbus code nor the lwIP IPv6 stack ends up in the firmware.
idf_component_register()
