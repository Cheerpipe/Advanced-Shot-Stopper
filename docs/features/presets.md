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
| Preset | Target, BBW, protection time, Fast/Slow/A→M guards, cup-protection options, accidental-touch protection, baseline and learned stop offset |
| Shared machine settings | Physical switch behavior, rinse, no-scale policy, all three tare switches and timing, cup detection, alerts, preferred scale, network |
| Home session | Quick Settings controls affect the current workflow. In particular, turning BBW off selects Manual without saving BBW off in the recipe. |

To make an intentional recipe change permanent, edit and save it in Settings.
Home and Settings can therefore display different BBW values.

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

Learned offset follows its preset when you switch recipes. To reset only that
learning, save **Baseline offset**, then choose
**Reset learned stop offset to baseline**. This is different from resetting a
whole recipe or performing a factory reset. **Reset** on a factory card restores
that recipe's factory values; it is available only for factory cards.

Example: duplicate Double, set a 40 g target and valid Fast/Slow recovery
weights around it, then save. Loading Single later does not overwrite the
40 g recipe or its learned offset.

Related: [BBW](brew-by-weight.md), [Fast](fast-extraction-guard.md),
[Slow](slow-extraction-guard.md), [cup protection](cup-protection.md).
