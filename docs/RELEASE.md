# Release checklist

Use this list to publish a firmware version. It assumes the
[validation policy](../VALIDATION.md) and the [build guide](BUILD.md).

1. Start from the release commit on `main` with a clean tree, untracked files
   included.
2. Set `VERSION` to the new `MAJOR.MINOR.PATCH`, higher than every published
   version (OTA refuses older images), and add its entry to
   [CHANGELOG](../CHANGELOG.md). Commit both.
3. Run `./scripts/dev validate --risk R3`. It builds the development and the
   release image of every official pair and checks each against its size
   baseline.
4. Complete the [manual test plan](MANUAL_TEST_PLAN.md) items that apply and
   record the HIL evidence, including one OTA update from the previous
   release.
5. Build each published pair with `./scripts/dev build --release --hardware
   <id> --machine <id>`. Check that the version shown has no `-dirty` and that
   the build used no `--development`, `--jtag`, `--no-auth-admin`, or
   `--flags`; remote machine control ships only as a separately named,
   documented opt-in image.
6. Publish each `openbrewbyweight.bin` with its `identity.json` from
   `artifacts/firmware/<hardware>--<machine>/<ELF SHA>/`, keep the matching
   `.elf` for crash analysis, and create the annotated `vX.Y.Z` tag on the
   release commit.
