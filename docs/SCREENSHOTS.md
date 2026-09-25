# Web UI screenshots

These captures illustrate the layout in dark and light modes. They are historical examples,
not a specification of current labels, defaults, build options or permissions.
Home's old No-scale toggle has been replaced by the
[mode-based policy](settings/no-scale-bbw.md). Use [first setup](GETTING_STARTED.md)
and the settings guides for current actions.

## Dark mode

### Home

Quick Settings, recipe, last shot and connection status. Remote actions depend
on build policy and Admin unlock: the Actions panel is shown only when remote
machine control is compiled in and Admin is unlocked.

![Historical Home screen with recipe and last-shot panels](images/screenshot-home-dark.jpeg)

### Settings

Recipe and machine/scale configuration. The current group depends on the
compiled paddle or momentary machine type.

![Historical dark Settings screen](images/screenshot-settings-dark.jpeg)

### Statistics

See [history](features/shot-history.md) for current eligibility, sorting,
ratings and export behavior.

![Historical dark Statistics screen](images/screenshot-stats-dark.jpeg)

### Diagnostics

Technical status for problem reports. A single indicator does not establish
safe actuation. Loop gap is the largest gap between loop starts in the recent
five-second window; Loop max is the largest since its last **Reset**. The table
breaks down the phases from each of those two events, even when the CPU
profiler is stopped. Delay call measures the pause until the task resumes,
including scheduling; loop dispatch measures the time from that return to the
next loop start. Other timing covers the remaining work outside the listed
phases. The figures may differ slightly from the rounded gap above. Select
**Reset** beside Loop max to begin a new maximum
measurement.

![Historical dark Diagnostics screen](images/screenshot-diagnostic-dark.jpeg)

### Admin

Administration panel for device password, Wi-Fi, OTA, and factory reset.

![Historical dark Admin screen](images/screenshot-admin-dark.jpeg)

## Light mode

The same documented controls apply in light mode.

### Home

![Historical light Home screen](images/screenshot-home-light.jpeg)

### Settings

![Historical light Settings screen](images/screenshot-settings-light.jpeg)

### Statistics

![Historical light Statistics screen](images/screenshot-stats-light.jpeg)

### Diagnostics

![Historical light Diagnostics screen](images/screenshot-diagnostic-light.jpeg)

### Admin

![Historical light Admin screen](images/screenshot-admin-light.jpeg)
