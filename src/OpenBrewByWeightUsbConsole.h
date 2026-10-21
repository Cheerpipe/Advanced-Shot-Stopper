#pragma once

// Default IDF builds compile with ARDUINO_USB_CDC_ON_BOOT=0, so Arduino's
// Serial is UART0 (GPIO 43/44). The GPIO4 jumper is supposed to start the
// native USB Serial/JTAG CDC, which is HWCDC — not UART0. Arduino 3.x only
// instantiates HWCDCSerial when CDC_ON_BOOT=1 (the JTAG extra flag).
// Provide our own HWCDC and alias Serial to it so Serial.begin() on the
// jumper path actually enumerates CDC. JTAG builds already map Serial to
// HWCDCSerial and skip this alias.

#if !defined(OPEN_BREW_BY_WEIGHT_HOST_TEST)
#include "HWCDC.h"
#if defined(SOC_USB_SERIAL_JTAG_SUPPORTED) && SOC_USB_SERIAL_JTAG_SUPPORTED && \
    defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE &&                           \
    !(defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT)
#define OPEN_BREW_BY_WEIGHT_USB_CONSOLE_OWN_HWCDC 1
extern HWCDC openBrewByWeightUsbConsole;
#ifdef Serial
#undef Serial
#endif
#define Serial ::openBrewByWeightUsbConsole
#endif
#endif
