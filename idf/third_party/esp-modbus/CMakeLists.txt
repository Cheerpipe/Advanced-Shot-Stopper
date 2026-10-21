# Empty stand-in for espressif/esp-modbus. arduino-esp32 declares esp-modbus
# as a public dependency although this firmware never uses Modbus. The override
# in main/idf_component.yml points here so no Modbus code ends up in the
# firmware.
idf_component_register()
