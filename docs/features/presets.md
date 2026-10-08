# Presets

Presets store brew recipes. Double is active on a new controller; Single is
also included. Open **Settings → Brew** while the machine is idle.
Settings shows each recipe as a card with its name, whether it is factory
or custom, and its target weight. On Home, the **Presets** section lists the
recipes as rows: the active one is marked with a filled circle and expands to
its Fast, BBW, and Slow cut rules, and tapping another row applies that
recipe when brewing by weight is on.

Each rule shows the recipe's configured weight and time conditions. Fast shows
both ways it can stop: the maximum recovery weight after BBW protection ends,
or the minimum brew time once the target is reached. Slow shows the minimum
recovery weight and when it becomes eligible. These are recipe values; learned
drip compensation and time limits still apply.

The preset list and New, Duplicate, Reset, and Delete buttons stay hidden
behind a loading animation until presets are ready. The other Settings
subsections remain available as collapsed groups.
Once loaded, the available actions depend on the selected preset and whether
editing is allowed.

## Change the target weight from Home

When the last shot card on Home shows a target (a scale shot, while the
machine is idle), a small pencil sits next to the target weight. Tap it to
open a sliding scale at the bottom of the screen:

- Drag slowly to move one gram at a time; drag faster and the scale
  accelerates so you can cross the range in a single gesture.
- The target can be set from 10 g to 200 g, in whole grams.
- **Reset** appears once you move away from the recipe's weight; tap it to go
  back.
- Closing the sheet — with **Done**, a swipe down, or tapping outside — saves
  the new weight to the active recipe right away. The change is the same as
  saving the target in Settings.

The last shot card only appears after the controller has recorded a shot.
Until then, opening `http://<device-address>/?edit_weight=1` brings up the
same sliding scale directly. The address parameter is used once and removed,
so refreshing the page afterwards shows the normal Home.

## Create or change a recipe

1. Load the preset you want to use.
2. Select **Duplicate** to start from that recipe, or **New** for an Untitled
   preset seeded from firmware Double defaults.
3. Rename the card, edit its brew settings, and select **Save preset**.
4. Switch to another preset and back to check the saved recipe.

Duplicate names receive a suffix, such as "Double copy 2". You can keep up to
eight presets in total, Single and Double included. Once you have eight, New
and Duplicate stop creating cards — delete a custom preset to make room for a
new one. Factory cards cannot be deleted. You can delete custom presets, but
not the last remaining preset. The active preset survives reboot.

## What is saved where

| Scope | Settings |
| --- | --- |
| Preset | Target, BBW, protection time, Fast/Slow/A→M guards, cup-protection options, accidental-touch protection, offset/alpha baselines, learned offset, EWMA gain and initial/learned provenance; Linea Micra builds also retain a brew-boiler target |
| Shared machine settings | Physical switch behavior, rinse, no-scale policy, all three tare switches and timing, cup detection, alerts, preferred scale, network |
| Home session | Quick Settings BBW affects the current workflow. Turning it off selects Manual without saving BBW off in the recipe. |
| Home / Home Assistant active preset | Quick guard switches persist Fast, Slow, A→M, cup protection, and accidental-touch values only in the active preset. |

To make an intentional recipe change permanent, edit and save it in Settings;
the target weight alone can also be set from Home as described above.
**Stop after sustained weight** is also saved per preset. New and factory-reset
recipes enable it; Duplicate copies the source recipe's choice. Turning
**Avoid accidental touch** off preserves that choice but disables the backup
and its editor. See [touch-stop backup](brew-by-weight.md#when-touch-protection-delays-a-stop).
Home and Settings can therefore display different BBW values.
Home Assistant follows the same scopes: BBW is session-only, No-scale BBW is a
shared machine policy, and the five guard switches persist only the active
preset without changing its target or learned values.
Save remains bound to the preset whose fields were loaded into the form; an
asynchronous active-preset change cannot redirect those values to another recipe.
The Linea Micra brew target accepts 80.0–100.0 °C in 0.1 °C steps and defaults
to 93.0 °C for new and factory-reset recipes. Duplicating a preset copies this value;
disabling the option or disconnecting the Micra account does not erase it. See
[Linea Micra settings](../settings/linea-micra.md).
With a connected Micra account and **Allow brew boiler temperature in presets**
enabled, a persisted active-preset change applies that value through the cloud.
A scale connection reapplies the active value. Shot or scale-connection activity
defers the request, rapid changes keep only the newest target, and success is
reported only after the Micra dashboard confirms the same temperature.

## Factory recipes

| Value | Single | Double |
| --- | ---: | ---: |
| Target | 18 g | 36 g |
| Baseline / initial learned offset | 0.5 g | 1.5 g |
| Fast: minimum brew time | 28 s | 28 s |
| Fast: maximum recovery weight | 20 g | 42 g |
| Slow: decision time | 44 s | 44 s |
| Slow: minimum recovery weight | 16 g | 34 g |

Both start with BBW and Fast/Slow/A→M enabled, Max BBW time 50 s and initial
BBW protection 12 s. These are factory seeds, not a description of a modified
or factory preset. Definitions: `fillFactorySinglePreset` /
`fillDoubleFirmwareDefaults` in
`OpenBrewByWeightPresets.h` in the firmware sources.

Learning follows the preset. New and factory recipes seed the offset from
the table above and use current/base α=0.30. Duplicating a preset copies the
baseline, offset and gain/provenance into an independent recipe with empty
candidate evidence. Reboot retains those saved values and rebuilds only the
transient evidence window. Updating from firmware that still offered
**Linear regression + offset correction** keeps that mode's offset as the
EWMA starting point.

Use a separate preset for each physical portafilter/basket setup. The learned
offset in grams, gain α and candidate observations belong to that preset; shots
from another preset do not train it. The candidate window is transient, while
offset/gain and the shot log are persistent. Changing the physical setup under
the same preset requires an intentional learning reset; it is not detected.

Save **Baseline offset**, then choose **Reset learned stop offset to baseline**
to restore the offset. The gain is retained but evidence restarts. Save
**Baseline learning factor (α)** (0.01–1.00, step 0.01), then
**Reset EWMA learning** restores both saved bases and initial provenance.
Changing either base alone preserves the current offset, gain and evidence.
**Reset** on a factory card restores the whole recipe, offset and initial
gain/base at 0.30; it is available only for factory cards. A device factory
reset additionally erases other settings and history. See
[BBW learning](brew-by-weight.md#cutoff-behavior-and-learning)
for prediction, gain selection and reset behavior.

Example: duplicate Double, set a 40 g target and valid Fast/Slow recovery
weights around it, then save. Loading Single later does not overwrite the
40 g recipe or its learned offset.

New history records capture the originating preset ID; CSV exports `preset_id`
alongside the cutoff metadata. Keep the ID-to-physical-setup mapping with exports;
see [history provenance](shot-history.md#read-a-result) for older records and ID lifecycle.

Related: [BBW](brew-by-weight.md), [Fast](fast-extraction-guard.md),
[Slow](slow-extraction-guard.md), [cup protection](cup-protection.md).
