# Alerts

Sounds and the optional onboard LED that tell you what the controller is
doing: tare, first drops, paddle left ON, scale lost or connected, and
extended-shot pulses.

This page covers both the feature and the **Settings → Alerts** group. All of
these settings are **machine-level** (not per-preset).

## When it applies

**Sound alerts** is the master switch (default ON). Off silences operational
alerts on both the local buzzer and the scale; the individual checkboxes are
kept.

Most sounds follow their event: tare/start/stop, first drop, paddle reminder,
completion extra, and ATM / no-scale / scale-link alerts follow **Output channel** when a
local buzzer is compiled in (`SHOT_STOPPER_ENABLE_BUZZER=1`, passive piezo
with RTTTL). `=0` omits the local buzzer. Channel selection is independent
of that flag, **except shot start and
stop**: those two cues always play on the local buzzer at the machine circuit relay
edge (close = start, open = stop), including auto, manual, and rinse.
They never wait for Bluetooth or for the scale timer to start or stop.

If the local tare cue is sounding or waiting when the machine circuit starts,
the shot-start cue cuts it off and plays immediately. A sound that the scale
itself has already started as command feedback cannot be recalled over
Bluetooth, so that scale sound may finish while the local shot-start cue begins.

Without buzzer support, Output channel and the local-only checkboxes are hidden.
The default channel is then **Scale only**.

Start/stop cues are local feedback, not independent proof that water flow
changed. On momentary machines, check the actual group and its state feedback.

## Parameters

| Setting | Default | Range / notes | Effect |
| --- | --- | --- | --- |
| **Sound alerts** | ON | ON / OFF | Master switch in **Settings → Alerts**. |
| **Output channel** | **Buzzer only** with a local buzzer; **Scale only** without | Buzzer only / Scale only / Scale priority | Where most alerts play. **Buzzer only**: all sound on the local buzzer. **Scale only**: scale path; scale-incapable local cues are muted. **Scale priority**: scale when connected/able, else buzzer; never both for one event. Shot **start** and **stop** always use the local buzzer at machine circuit when a buzzer is compiled in. Shown only with buzzer support. |
| **Beep when coffee starts** | ON | ON / OFF | One beep on first coffee drops during an automatic shot. Ignored when brew by weight is off. |
| **Paddle-off reminder** | ON | ON / OFF | Repeat beeps while the **physical paddle stays ON** and **machine circuit is already open**. |
| **Paddle reminder interval (s)** | 10 s | 5–60 s | Time between reminder beeps. |
| **Paddle reminder limit (min)** | 15 min | 1–60 min | Stop beeping after this time even if the paddle remains ON. |
| **Scale lost** | (buzzer builds) | ON / OFF | Descending RTTTL on the local buzzer when the scale disconnects (idle or during a shot). Hidden / disabled when Output channel is Scale only. |
| **ATM / manual-no-scale** | (buzzer builds) | ON / OFF | Distinct RTTTL on the local buzzer when A→M ends or when BBW needs a scale that is missing. Disabled when Output channel is Scale only. |
| **Scale connected** | ON (Buzzer only or Scale priority) | ON / OFF | Rising RTTTL when a scale connects or reconnects. Always the local buzzer. |
| **Blue LED while scale connected** | ON | ON / OFF | Onboard GPIO 1 HIGH while a scale is BLE-connected. Independent of Sound alerts. |
| **Extended shot pulse** | Fast | Disabled / Slow / Medium / Fast / Rapid | Local-buzzer pulses while Fast extraction guard keeps the shot running past the normal cut. Disabled when Output channel is Scale only. |
| **Slow extended pulse** | Fast | Same rates | Same idea for Slow extraction guard. |
| **Bullseye melody** | OFF | ON / OFF; Buzzer only | Plays the saved custom RTTTL tune once when a finished shot remains exactly at its target weight for at least 1 full second. The text is retained when disabled. |
| **Custom RTTTL** | empty | Up to 500 ASCII characters / 250 notes | Validated when saved and parsed into a fixed-capacity buffer. Editable only while Bullseye melody is enabled; otherwise it remains visible and read-only. |

## Bullseye timing

Bullseye applies to automatic, timer-only, and manual shots, but not rinses.
It is active only when **Sound alerts** is on, **Output channel** is **Buzzer
only**, and a local buzzer is compiled in.

Tracking starts when the configured drip delay finishes. Repeated fresh scale
samples must report the target exactly for a continuous 1 full second; a
different weight or a gap longer than the normal 1-second automation freshness
limit restarts the stability timer. There is no further time limit while the
same cup remains on the scale. Removing or replacing that cup, or starting
another cycle, discards the pending Bullseye.

In **Buzzer only** (and Scale priority when the scale is not usable),
tare/retare sounds follow paddle/retare immediately and do not wait for a
Bluetooth round-trip. Shot **start** and **stop** always follow machine circuit on the
local buzzer whenever one is compiled in, including Scale only and Scale
priority with a connected scale. The scale timer may keep running briefly
after machine circuit opens; that does not delay the stop beep.

**Mute scale in Buzzer only** and **Scale volume** live under
[Scales](settings/scales.md). They change scale speaker behavior, not this
Alerts group.

## Example

Brew by weight finishes, machine circuit opens, but you left the paddle ON. The
paddle-off reminder repeats every 10 s for up to 15 minutes until you return
the paddle to OFF.

## Choose an output

Use **Buzzer only** for local cues with a compiled passive buzzer. Use
**Scale only** if the scale supports the requested sound. **Scale priority**
falls back to the buzzer for unsupported/unavailable scale sounds.
Start/stop local cues are the exception described above.

If a sound is missing, check Sound alerts, the output mode, the individual
checkbox and [scale capabilities](settings/scales.md) before changing wiring.
A scale can support tare/timer without supporting arbitrary beeps.
Custom RTTTL follows `name:defaults:notes`, for example
`done:d=8,o=5,b=120:c,e,g`; save it with Bullseye enabled to test its validation.

Related: [A→M time guard](features/auto-to-manual.md),
[Fast extraction guard](features/fast-extraction-guard.md),
[Slow extraction guard](features/slow-extraction-guard.md),
[Hardware](HARDWARE.md) (buzzer GPIO and LED).
