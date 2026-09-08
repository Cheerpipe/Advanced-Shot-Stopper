# Presets

Presets store brew recipes. Double is active on a new controller; Single is
also included. Open **Settings → Brew** while the machine is idle.

## Create or change a recipe

1. Load the preset you want to use.
2. Select **Duplicate** to start from that recipe, or **New** for an Untitled
   preset seeded from firmware Double defaults.
3. Rename the card, edit its brew settings, and select **Save preset**.
4. Switch to another preset and back to check the saved recipe.

Duplicate names receive a suffix, such as "Double copy 2". Factory cards cannot be deleted. You can delete custom
presets, but not the last remaining preset. The active preset survives reboot.

## What is saved where

| Scope | Settings |
| --- | --- |
| Preset | Target, BBW, cutoff algorithm, protection time, Fast/Slow/A→M guards, cup-protection options, accidental-touch protection, offset/alpha baselines, separate regression/EWMA offsets, EWMA gain and initial/learned provenance |
| Shared machine settings | Physical switch behavior, rinse, no-scale policy, all three tare switches and timing, cup detection, alerts, preferred scale, network |
| Home session | Quick Settings controls affect the current workflow. In particular, turning BBW off selects Manual without saving BBW off in the recipe. |

To make an intentional recipe change permanent, edit and save it in Settings.
Home and Settings can therefore display different BBW values.
Save remains bound to the preset whose fields were loaded into the form; an
asynchronous active-preset change cannot redirect those values to another recipe.

## Factory recipes

| Value | Single | Double |
| --- | ---: | ---: |
| Target | 18 g | 36 g |
| Baseline / initial learned offset | 0.5 g | 1.5 g |
| Fast: minimum brew time | 28 s | 28 s |
| Fast: maximum recovery weight | 20 g | 42.5 g |
| Slow: decision time | 44 s | 44 s |
| Slow: minimum recovery weight | 16 g | 34 g |

Both start with BBW and Fast/Slow/A→M enabled, Max BBW time 50 s and initial
BBW protection 12 s. These are factory seeds, not a description of a modified
or migrated preset. Definitions: `fillFactorySinglePreset` /
`fillDoubleFirmwareDefaults` in
[ShotStopperPresets.h](../../src/ShotStopperPresets.h).

Both algorithms' learning follows the preset. New and factory recipes select
adaptive EWMA, seed both offsets from the table above and use current/base α=0.30. Duplicate
copies selection, both bases, offsets and gain/provenance into an independent recipe,
with empty candidate evidence. Reboot retains those saved values and rebuilds
only the transient evidence window. Upgrades without a selector preserve regression's
offset and copy it to EWMA, selecting EWMA once; later updates retain the saved
choice. Switching back to **Linear regression + offset correction** resumes its own offset.
Its API/CSV identifier remains `legacy`.

Use a separate preset for each physical portafilter/basket setup. EWMA's learned
offset in grams, gain α and candidate observations belong to that preset; shots
from another preset do not train it. The candidate window is transient, while
offset/gain and the shot log are persistent. Changing the physical setup under
the same preset requires an intentional learning reset; it is not detected.

Save **Baseline offset**, then choose **Reset learned stop offset to baseline**
to reset only the selected algorithm's offset. EWMA retains its gain but restarts
evidence. Save **Baseline learning factor (α)** (0.01–1.00, step 0.01), then
**Reset EWMA learning** restores both saved bases and initial provenance.
Changing either base alone preserves current offsets, gain and evidence.
The editable baseline is shared by both algorithms within one preset; their
learned offsets are independent. For example, save a 0.80 g baseline for one
portafilter, select EWMA and reset: its EWMA offset becomes 0.80 g, while its
regression offset and every other preset remain unchanged.
**Reset** on a factory card restores the whole recipe, both offsets and initial
gain/base at 0.30; it is available only for factory cards. A device factory reset additionally
erases other settings and history. See [BBW learning](brew-by-weight.md#cutoff-algorithms-and-learning)
for prediction, gain selection and reset behavior.

Example: duplicate Double, set a 40 g target and valid Fast/Slow recovery
weights around it, then save. Loading Single later does not overwrite the
40 g recipe or its learned offset.

New history records capture the originating preset ID; CSV exports `preset_id`
alongside the cutoff metadata. Keep the ID-to-physical-setup mapping with exports;
see [history provenance](shot-history.md#read-a-result) for older records and ID lifecycle.

Related: [BBW](brew-by-weight.md), [Fast](fast-extraction-guard.md),
[Slow](slow-extraction-guard.md), [cup protection](cup-protection.md).
