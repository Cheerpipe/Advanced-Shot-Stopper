// A wake gesture is relay-only. The Micra adapter owns power classification;
// the stopper owns the temporary passthrough and keeps it outside brew/guards.
bool serviceMachineWakeGesture(const MachineIntention &intent) {
  machineWakeGestureConsumedThisLoop = false;
  if (machineWakePassthroughActive) {
    machineWakeGestureConsumedThisLoop = true;
    if (intent.turnedOff || intent.stablyOff || !intent.holdActive) {
      machineRequestStop();
      machineWakePassthroughActive = false;
    }
    return true;
  }
  if (!intent.turnedOn) return false;

  const MachinePhysicalStartDisposition disposition =
      machineIntegrationPhysicalStart();
  if (disposition != MachinePhysicalStartDisposition::WAKE_PASSTHROUGH) {
    return false;
  }

  machineWakeGestureConsumedThisLoop = true;
  if (!firmwareInitializationComplete || stopperState != StopperState::READY ||
      session.active || maintenanceLease.active) {
    return true;
  }
  machineSetActivatorDriveAllowed(true);
  machineAllowCurrentActivatorHold();
  if (!servicePowerManagement() ||
      !machineRequestStart(HARD_MAX_CIRCUIT_CLOSED_MS, false)) {
    transitionTo(StopperState::REQUIRES_OFF);
    return true;
  }
  machineWakePassthroughActive = true;
  return true;
}
