namespace {
// Formats a bounded non-negative stat, trimming to `decimals` places.
const char *formatStatFloat(char (&buffer)[32], double value, unsigned decimals) {
  snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  return buffer;
}
}  // namespace

// Pre-computed stats over the newest shots (WebUI Stats card mirror).
// External linkage with a header declaration: the shots-page handler is
// inlined into this translation unit before this include.
bool buildIntegrationStats(const ShotStatsView &stats, char *output,
                           size_t capacity) {
  char values[5][32];
  const int written = snprintf(
      output, capacity,
      "{\"shotCount\":%lu,\"totalDurationS\":%.1f,\"avgDurationS\":%s,"
      "\"avgYieldG\":%s,\"avgErrorPct\":%s,\"avgFlowGps\":%s,"
      "\"shotsPerDay\":%s}",
      static_cast<unsigned long>(stats.shotCount),
      static_cast<double>(stats.durationDsSum) / 10.0,
      stats.shotCount > 0
          ? formatStatFloat(values[0], static_cast<double>(stats.durationDsSum) /
                            (10.0 * stats.shotCount), 1)
          : "null",
      stats.shotCount > 0
          ? formatStatFloat(values[1], static_cast<double>(stats.actualCgSum) /
                            (100.0 * stats.shotCount), 1)
          : "null",
      stats.bbwCount > 0
          ? formatStatFloat(values[2], static_cast<double>(stats.errorPctTenthsSum) /
                            stats.bbwCount / 10.0, 1)
          : "null",
      stats.flowCount > 0
          ? formatStatFloat(values[3], static_cast<double>(stats.flowCgSx100Sum) /
                                stats.flowCount / 10000.0,
                            2)
          : "null",
      stats.timedCount > 0
          ? formatStatFloat(values[4],
                static_cast<double>(stats.timedCount) /
                    (static_cast<double>(stats.daysSpan) + 1.0),
                1)
          : "null");
  if (written <= 0 || static_cast<size_t>(written) >= capacity) return false;
  size_t used = static_cast<size_t>(written) - 1U;
  int added = snprintf(output + used, capacity - used, ",\"durationsS\":[");
  if (added <= 0 || static_cast<size_t>(added) >= capacity - used) return false;
  used += static_cast<size_t>(added);
  for (uint32_t i = 0; i < stats.shotCount; ++i) {
    added = snprintf(output + used, capacity - used, "%s%.1f",
                     i == 0 ? "" : ",", stats.durationsDs[i] / 10.0);
    if (added <= 0 || static_cast<size_t>(added) >= capacity - used) return false;
    used += static_cast<size_t>(added);
  }
  return capacity - used > 2 &&
         snprintf(output + used, capacity - used, "]}") == 2;
}
