bool servicePowerManagement(bool physicalEdge = false,
                            const ScaleLinkSnapshot &link = getScaleLinkSnapshot()) {
  const uint32_t now = millis();
  PowerInputs inputs;
  inputs.enabled = runtimeConfig.powerManagementEnabled;
  inputs.machineBusy = session.active ||
      machineRunState() != MachineRunState::IDLE ||
      relayCircuitClosed();
  inputs.scaleBusy = link.connecting || scaleLinkAvailable(link) ||
                     powerScaleBusy.load(std::memory_order_acquire);
  inputs.physicalEdge = physicalEdge;
  inputs.webActive = powerWebActive(now);
  inputs.maintenance = maintenanceLease.active ||
      powerNetworkBusy.load(std::memory_order_acquire) ||
      usbSerialEnableSource != UsbSerialEnableSource::OFF ||
      !bootCapabilities.mandatorySafetyReady();
  const PowerProfile desired = powerPolicy.update(now, inputs);
  powerCooldownRemainingMs.store(powerPolicy.cooldownRemaining(now),
                                 std::memory_order_relaxed);
  if (applyPowerProfile(desired)) return true;
  platformClockReady = false;
  tripRelaySafety(RelaySafetyFault::INITIALIZATION_FAILED);
  requestSafeRestart();
  return false;
}
