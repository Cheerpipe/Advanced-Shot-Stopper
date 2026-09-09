bool servicePowerManagement(bool physicalEdge = false) {
  const uint32_t now = millis();
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  PowerInputs inputs;
  inputs.enabled = runtimeConfig.powerManagementEnabled;
  inputs.machineBusy = session.active ||
      machineRunState() != MachineRunState::CONFIRMED_OFF ||
      getRelaySafetySnapshot().closed;
  inputs.weighted = session.active &&
      session.weightControlState != WeightControlState::INACTIVE;
  inputs.rinse = stopperState == StopperState::RINSE;
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
