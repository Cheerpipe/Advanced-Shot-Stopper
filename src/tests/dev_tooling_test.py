#!/usr/bin/env python3
"""Golden contracts for the developer facade."""

import subprocess
from pathlib import Path
import os
import re
import csv
import io
import runpy
import shutil
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
DEV = ROOT / "scripts/dev"
INTERNAL = ROOT / "scripts/internal"


# Check rendered-document targets without depending on the working tree's docs.
dev_module = runpy.run_path(str(DEV))
doc_check = dev_module["markdown_errors"]
with tempfile.TemporaryDirectory(prefix="shotstopper-docs-") as temporary:
    fixture = Path(temporary)
    (fixture / "docs").mkdir()
    (fixture / "libraries/EspressoScaleBLE").mkdir(parents=True)
    (fixture / "docs/guide.md").write_text(
        '# Guide\n## Repeated\n## Repeated\n<a id="old-section"></a>\n')
    (fixture / "docs/photo.png").write_bytes(b"test fixture")
    (fixture / "docs/with space.md").write_text('# Spaced\n')
    (fixture / "README.md").write_text(
        '# Home\n[wrapped\nlink](docs/guide.md#repeated-1)\n'
        '[alias](docs/guide.md#old-section)\n[space](<docs/with space.md#spaced>)\n'
        '![photo](docs/photo.png)\n[local](#home)\n'
        '[external](https://example.org/missing#ignored)\n'
        '```md\n[example](missing-example.md)\n```\n')
    for directory in ("temp/ai_temp_check", "docs/plans", "docs/audits/nested"):
        local = fixture / directory
        local.mkdir(parents=True)
        (local / "review.md").write_text('# Local record\n[broken](missing.md)\n')
    with patch.dict(doc_check.__globals__, ROOT=fixture, AREAS={}):
        assert doc_check() == [], doc_check()
        canonical = fixture / "docs/audits-guide.md"
        canonical.write_text('# Canonical guide\n[broken](missing.md)\n')
        errors = doc_check()
        assert len(errors) == 1 and 'docs/audits-guide.md:' in errors[0], errors
        canonical.write_text('# Canonical guide\n')
        (fixture / "libraries/EspressoScaleBLE/README.md").write_text(
            '# Library\n[bad](../../docs/guide.md#missing)\n'
            '[ref]: ../../docs/absent.md\n<img src="missing.png">\n')
        errors = doc_check()
        assert len(errors) == 3, errors
        assert any('missing anchor' in error for error in errors), errors
        assert all('libraries/EspressoScaleBLE/README.md:' in error for error in errors)


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(DEV), "--verbosity", "compact", *args],
                          cwd=ROOT, text=True, capture_output=True)


golden = {
    ".github/workflows/validation.yml": "R1",
    ".gitignore": "R0",
    "README.md": "R0",
    "src/web/app.js": "R1",
    "scripts/dev": "R1",
    "libraries/EspressoScaleBLE/src/ScaleProtocol.h": "R2",
    "src/ShotStopperNetwork.cpp": "R2",
    "src/ShotStopperOta.cpp": "R2",
    "src/ShotStopperMachineRelay.h": "R3",
    "idf/partitions-n8r4.csv": "R3",
    "unknown/new.file": "R3",
}
for path, expected in golden.items():
    result = run("classify", path)
    assert result.returncode == 0, result.stderr
    assert f"risk={expected}" in result.stdout, (path, result.stdout)

lower = run("validate", "--risk", "R0", "src/ShotStopperMachineRelay.h")
assert lower.returncode == 2 and "cannot lower" in lower.stderr

unsafe = run("flash", "--arch", "n8r4")
assert unsafe.returncode == 2 and "requires --confirm" in unsafe.stderr
secret = run("ota", "--confirm", "--password", "do-not-log")
assert secret.returncode == 2 and "never argv" in secret.stderr
obsolete = run("ota", "--confirm", "--force")
assert obsolete.returncode == 2 and "--yes" in obsolete.stderr and \
    "--wait-for-confirmation" in obsolete.stderr
for invalid in (("build", "monitor"), ("build", "flash", "ota", "--confirm"),
                ("monitor", "--host", "controller.local"),
                ("build", "flash", "--confirm", "--image", "firmware.bin")):
    result = run(*invalid)
    assert result.returncode == 2, (invalid, result.stdout, result.stderr)


def captured_firmware(*args: str, stdin: str = "") -> dict:
    captured = {}

    def capture(command, risk, steps, verbosity, manual=None, prelude=None,
                interactive=False, env_extra=None):
        captured.update(command=command, risk=risk, steps=steps, manual=manual,
                        interactive=interactive, env_extra=env_extra or {})
        return 0

    main = dev_module["main"]
    with patch.dict(main.__globals__, execute=capture), \
            patch.object(sys, "argv", [str(DEV), *args]), \
            patch.object(sys, "stdin", io.StringIO(stdin)):
        assert main() == 0
    return captured


for pipeline in (("build",), ("flash",), ("ota",), ("monitor",),
                 ("build", "flash"), ("build", "ota"),
                 ("flash", "monitor"), ("ota", "monitor"),
                 ("build", "flash", "monitor"),
                 ("build", "ota", "monitor")):
    invocation = list(pipeline)
    if {"flash", "ota"}.intersection(pipeline):
        invocation.append("--confirm")
    captured = captured_firmware(*invocation)
    child = captured["steps"][0][1]
    assert child[:len(pipeline) + 1] == [
        "./scripts/internal/firmware-idf", *pipeline], (pipeline, child)
    assert "--confirm" not in child
    assert "--flags=" not in child

jtag_forwarded = captured_firmware("build", "--jtag")
assert jtag_forwarded["steps"][0][1][-2:] == ["--", "--jtag"]
jtag_rejected = run("monitor", "--jtag")
assert jtag_rejected.returncode == 2 and "does not apply" in jtag_rejected.stderr
flash_jtag_rejected = run("flash", "monitor", "--jtag")
assert flash_jtag_rejected.returncode == 2 and "does not apply" in flash_jtag_rejected.stderr

stdin_password = captured_firmware(
    "ota", "--confirm", "--yes", "--password-stdin", stdin="stdin-secret\n")
assert stdin_password["env_extra"] == {
    "SHOTSTOPPER_DEVICE_PASSWORD": "stdin-secret"}
assert "stdin-secret" not in repr(stdin_password["command"] +
                                  stdin_password["steps"][0][1])

for area in ("safety", "control", "machine", "scale", "ble", "network",
             "ota", "persistence", "web", "build", "tests"):
    result = run("context", area)
    assert result.returncode == 0 and area in result.stdout, (area, result.stderr)

dry_run = run("clean", "--dry-run")
assert dry_run.returncode == 0 and "build-host" in dry_run.stdout

for script in (ROOT / "scripts").rglob("*"):
    if not script.is_file() or script.suffix == ".js":
        continue
    first = script.read_text(errors="replace").splitlines()[0]
    if "sh" in first:
        checked = subprocess.run(["bash", "-n", str(script)], capture_output=True)
        assert checked.returncode == 0, f"shell syntax: {script.name}"


def partition_rows(name: str) -> dict[str, tuple[int, int]]:
    with (ROOT / "idf" / name).open(newline="") as handle:
        rows = {}
        for row in csv.reader(line for line in handle if not line.startswith("#")):
            if not row:
                continue
            rows[row[0].strip()] = (int(row[3].strip(), 0),
                                    int(row[4].strip(), 0))
        return rows


partition_contracts = {
    "partitions-n16r8.csv": {
        "flash": 0x1000000,
        "rows": {"nvs": (0x9000, 0x15000), "otadata": (0x1E000, 0x2000),
                 "app0": (0x20000, 0x300000), "app1": (0x320000, 0x300000),
                 "shotcurve": (0x620000, 0xA000),
                 "shotlog": (0x62A000, 0x8000),
                 "history": (0x632000, 0x8000),
                 "ffat": (0x63A000, 0x9B6000),
                 "coredump": (0xFF0000, 0x10000)},
    },
    "partitions-n8r4.csv": {
        "flash": 0x800000,
        "rows": {"nvs": (0x9000, 0x15000), "otadata": (0x1E000, 0x2000),
                 "app0": (0x20000, 0x330000), "app1": (0x350000, 0x330000),
                 "shotcurve": (0x680000, 0xA000),
                 "shotlog": (0x68A000, 0x8000),
                 "history": (0x692000, 0x8000),
                 "spiffs": (0x69A000, 0x156000),
                 "coredump": (0x7F0000, 0x10000)},
    },
}
for filename, contract in partition_contracts.items():
    rows = partition_rows(filename)
    text = (ROOT / "idf" / filename).read_text()
    assert rows == contract["rows"], (filename, rows)
    assert "shotcurve,data, 0x40" in text, "dedicated curve subtype changed"
    ordered = sorted(rows.items(), key=lambda item: item[1][0])
    for (_, (offset, size)), (_, (next_offset, _)) in zip(ordered, ordered[1:]):
        assert offset + size <= next_offset, f"partition overlap in {filename}"
    last_offset, last_size = ordered[-1][1]
    assert last_offset + last_size == contract["flash"]
    assert rows["shotcurve"][1] == 40 * 1024, "shot-curve partition changed"
    assert rows["shotlog"][1] == 32 * 1024, "shot-log partition changed"
    assert rows["history"][1] == 32 * 1024, "activation-history partition changed"

flash_idf = (INTERNAL / "flash-idf").read_text()
for required in ("read_flash 0x8000 0x1000", "installed_nvs_bytes != 0x15000",
                 "installed_layout=blank", "installed_shotcurve_row",
                 "required_shotcurve_offset=0x680000", "0x620000", "erase_flash",
                 '"$installed_app0_offset" "$image"'):
    assert required in flash_idf, f"flash-idf migration contract missing: {required}"
assert '0x10000 "$image"' not in flash_idf, \
    "external app offset must be dynamic"
parsed_erase = subprocess.run(
    ["bash", "-c",
     f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
     'ss_cli_parse --erase-all; test "$SS_CLI_ERASE_ALL" = 1'],
    cwd=ROOT, capture_output=True, text=True)
assert parsed_erase.returncode == 0, parsed_erase.stderr
for controls, expected in (((), "0|0|"), (("--yes",), "1|0|--yes"),
                           (("--wait-for-confirmation",),
                            "0|1|--wait-for-confirmation"),
                           (("--yes", "--wait-for-confirmation"),
                            "1|1|--yes --wait-for-confirmation")):
    parsed_ota_controls = subprocess.run(
        ["bash", "-c", 'source "$1"; shift; ss_cli_parse "$@"; '
         'ss_cli_flags_for yes wait_for_confirmation; '
         'printf "%s|%s|%s" "$SS_CLI_YES" "$SS_CLI_WAIT_FOR_CONFIRMATION" '
         '"${SS_CLI_FORWARD[*]}"', "ota-controls",
         str(ROOT / "scripts/shotstopper_cli.sh"), *controls],
        cwd=ROOT, capture_output=True, text=True)
    assert parsed_ota_controls.returncode == 0 and \
        parsed_ota_controls.stdout == expected
removed_force = subprocess.run(
    ["bash", "-c",
     f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; ss_cli_parse --force'],
    cwd=ROOT, capture_output=True, text=True)
assert removed_force.returncode == 2 and "--wait-for-confirmation" in \
    removed_force.stderr
profile_cli = subprocess.run(
    ["bash", "-c",
     f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
     'ss_cli_parse --hardware esp32-s3-relay-x1-speaker '
     '--machine rancilio-silvia-pro-x --development --flags=-DCUSTOM=1; '
     'ss_cli_flags_for hardware_config machine_config development; '
     'printf "%s|%s|%s" "$(ss_get hardware_config)" '
     '"$(ss_get machine_config)" "${SS_CLI_FORWARD[*]}"'],
    cwd=ROOT, capture_output=True, text=True)
assert profile_cli.returncode == 0, profile_cli.stderr
assert profile_cli.stdout == (
    "esp32-s3-relay-x1-speaker|rancilio-silvia-pro-x|"
    "--hardware esp32-s3-relay-x1-speaker --machine rancilio-silvia-pro-x "
    "--development")
legacy_profile_cli = subprocess.run(
    ["bash", "-c",
     f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
     'ss_cli_parse --hardware-config old-hardware.json '
     '--machine-config old-machine.json'],
    cwd=ROOT, capture_output=True, text=True)
assert legacy_profile_cli.returncode == 0


def cli_probe(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["bash", "-c",
         f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
         'ss_cli_parse "$@" || exit $?; ss_cli_effective_flags',
         "probe", *args],
        cwd=ROOT, capture_output=True, text=True)


jtag_append = cli_probe("--jtag", "--flags=-DCUSTOM=1")
assert jtag_append.returncode == 0 and \
    jtag_append.stdout == "-DCUSTOM=1 -DSHOT_STOPPER_ENABLE_JTAG=1"
jtag_existing = cli_probe("--jtag", "--flags=-DSHOT_STOPPER_ENABLE_JTAG=1")
assert jtag_existing.returncode == 0 and \
    jtag_existing.stdout == "-DSHOT_STOPPER_ENABLE_JTAG=1"
jtag_with_dev = cli_probe("--jtag", "--development")
assert jtag_with_dev.returncode == 0 and jtag_with_dev.stdout == \
    "-DSHOT_STOPPER_ENABLE_JTAG=1 -DSHOT_STOPPER_DEVELOPMENT=1"
jtag_off_untouched = cli_probe("--flags=-DSHOT_STOPPER_ENABLE_JTAG=0")
assert jtag_off_untouched.returncode == 0 and \
    jtag_off_untouched.stdout == "-DSHOT_STOPPER_ENABLE_JTAG=0"
jtag_conflict = cli_probe("--jtag", "--flags=-DSHOT_STOPPER_ENABLE_JTAG=0")
assert jtag_conflict.returncode == 2 and \
    "conflicts with SHOT_STOPPER_ENABLE_JTAG=0" in jtag_conflict.stderr
jtag_valued = cli_probe("--jtag=1")
assert jtag_valued.returncode == 2 and "does not take a value" in jtag_valued.stderr
jtag_forward = subprocess.run(
    ["bash", "-c",
     f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
     'ss_cli_parse "$@" || exit $?; ss_cli_flags_for development jtag; '
     'printf "%s" "${SS_CLI_FORWARD[*]}"', "probe", "--jtag"],
    cwd=ROOT, capture_output=True, text=True)
assert jtag_forward.returncode == 0 and jtag_forward.stdout == "--jtag"
with tempfile.TemporaryDirectory(prefix="shotstopper-jtag-store-") as temporary:
    jtag_saved = subprocess.run(
        ["bash", "-c",
         f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
         'ss_cli_parse "$@" || exit $?; ss_cli_save; cat "$SS_CLI_STORE"',
         "probe", "--jtag", "--flags=-DCUSTOM=1"],
        cwd=ROOT, env={**os.environ, "SS_CLI_ROOT": temporary},
        capture_output=True, text=True)
    assert jtag_saved.returncode == 0 and \
        "flags=-DCUSTOM=1" in jtag_saved.stdout and \
        "jtag" not in jtag_saved.stdout
assert "deprecated; use --hardware" in legacy_profile_cli.stderr
assert "deprecated; use --machine" in legacy_profile_cli.stderr
development_conflict = subprocess.run(
    ["bash", "-c",
     f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
     'ss_cli_parse --development --flags=-DSHOT_STOPPER_DEVELOPMENT=0; '
     'ss_cli_effective_flags'], cwd=ROOT, capture_output=True, text=True)
assert development_conflict.returncode == 2 and \
    "conflicts" in development_conflict.stderr

with tempfile.TemporaryDirectory(prefix="shotstopper-defaults-") as temporary:
    defaults_root = Path(temporary)
    store = defaults_root / ".shotstopper"
    store.write_text("host=stored.local\nspeed=115200\nport=/missing/device\n")
    defaults_env = {**os.environ, "SS_CLI_ROOT": str(defaults_root),
                    "SHOTSTOPPER_NONINTERACTIVE": "1",
                    "SHOTSTOPPER_HOST": "environment.local",
                    "SHOTSTOPPER_DEVICE_PASSWORD": "environment-secret"}
    resolved = subprocess.run(
        ["bash", "-c",
         f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
         'ss_cli_parse --host cli.local; '
         'ss_cli_resolve "host speed password"; '
         'printf "RESULT:%s|%s|%s" "$(ss_get host)" "$(ss_get speed)" '
         '"$(ss_origin password)"'],
        cwd=ROOT, env=defaults_env, capture_output=True, text=True)
    assert resolved.returncode == 0 and \
        "RESULT:cli.local|115200|env" in resolved.stdout
    saved = store.read_text()
    assert "host=cli.local" in saved and "speed=115200" in saved
    assert "\npassword=" not in saved and "environment-secret" not in saved
    stale = subprocess.run(
        ["bash", "-c",
         f'source "{ROOT / "scripts/shotstopper_board.sh"}"; '
         f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
         'ss_cli_resolve "port"'], cwd=ROOT, env=defaults_env,
        capture_output=True, text=True)
    assert stale.returncode == 2 and "does not exist" in stale.stderr
    detected = subprocess.run(
        ["bash", "-c",
         f'source "{ROOT / "scripts/shotstopper_board.sh"}"; '
         f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
         'shotstopper_detect_ports(){ printf "/dev/null\\n"; }; '
         'ss_prompt_suggestion port'], cwd=ROOT, env=defaults_env,
        capture_output=True, text=True)
    assert detected.returncode == 0 and detected.stdout == "/dev/null"

missing_profiles = subprocess.run(
    [str(INTERNAL / "build-idf"), "--flags="], cwd=ROOT,
    env={**os.environ, "SHOTSTOPPER_NONINTERACTIVE": "1",
         "SHOTSTOPPER_HARDWARE": "", "SHOTSTOPPER_MACHINE": ""},
    capture_output=True, text=True)
assert missing_profiles.returncode == 2
assert "hardware_config machine_config" in missing_profiles.stderr
erase_image = subprocess.run(
    [str(INTERNAL / "flash-idf"), "--erase-all", "--image", "external.bin"],
    cwd=ROOT, capture_output=True, text=True)
assert erase_image.returncode == 2 and "cannot be combined" in erase_image.stderr


def language_cli(*args: str, environment: str | None = None):
    with tempfile.TemporaryDirectory(prefix="shotstopper-language-") as temporary:
        root = Path(temporary)
        (root / ".shotstopper").write_text("webui_language=fr\n")
        env = os.environ.copy()
        env.update(SS_CLI_ROOT=str(root), SHOTSTOPPER_NONINTERACTIVE="1")
        if environment is None:
            env.pop("SHOTSTOPPER_WEBUI_LANGUAGE", None)
        else:
            env["SHOTSTOPPER_WEBUI_LANGUAGE"] = environment
        command = (
            f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
            'ss_cli_parse "$@" || exit $?; ss_cli_resolve "webui_language" || exit $?; '
            'ss_cli_flags_for webui_language; '
            'printf "RESULT:%s|%s|%s\n" "$(ss_get webui_language)" '
            '"$(ss_origin webui_language)" "${SS_CLI_FORWARD[*]}"')
        result = subprocess.run(["bash", "-c", command, "probe", *args],
                                env=env, capture_output=True, text=True)
        stored = (root / ".shotstopper").read_text()
        return result, stored


default_language, default_store = language_cli()
assert default_language.returncode == 0 and \
    "RESULT:en|default|--webui-language en" in default_language.stdout
assert "Defaults:" in default_language.stdout and "webui_language=" not in default_store
env_language, env_store = language_cli(environment="EN_us")
assert "RESULT:en-us|env|--webui-language en-us" in env_language.stdout
assert "webui_language=" not in env_store
flag_language, flag_store = language_cli("--webui-language=En_GB", environment="fr")
assert "RESULT:en-gb|cli|--webui-language en-gb" in flag_language.stdout
assert "webui_language=" not in flag_store
invalid_language, _ = language_cli("--webui-language", "../en")
assert invalid_language.returncode == 2 and "Invalid WebUI language" in invalid_language.stderr


def validate_language_steps(args: list[str], environment: str | None = None):
    captured = {}

    def capture(command, risk, steps, verbosity, manual=None, prelude=None):
        captured.update(command=command, risk=risk, steps=steps)
        return 0

    main = dev_module["main"]
    with patch.dict(main.__globals__, execute=capture, markdown_errors=lambda: []), \
            patch.object(sys, "argv", [str(DEV), "validate", "--risk", "R3", *args]), \
            patch.dict(os.environ, {}, clear=False):
        if environment is None:
            os.environ.pop("SHOTSTOPPER_WEBUI_LANGUAGE", None)
        else:
            os.environ["SHOTSTOPPER_WEBUI_LANGUAGE"] = environment
        assert main() == 0
    return captured["steps"]


for validate_args, environment, expected in (
        (["src/web/app.js"], None, "en"),
        (["src/web/app.js"], "EN_us", "en-us"),
        (["--webui-language=FR_ca", "src/web/app.js"], "en", "fr-ca")):
    validate_steps = validate_language_steps(validate_args, environment)
    build_steps = [argv for name, argv in validate_steps
                   if name.startswith(("idf-", "warnings-"))]
    assert len(build_steps) == 6
    assert all(argv.count("--webui-language") == 1 and
               argv[argv.index("--webui-language") + 1] == expected
               for argv in build_steps), build_steps


def dispatched(stages: tuple[str, ...], args: list[str], fail: str = "",
               store: str | None = None) -> tuple[subprocess.CompletedProcess[str], list[str]]:
    with tempfile.TemporaryDirectory(prefix="shotstopper-wrapper-") as temporary:
        root = Path(temporary)
        scripts = root / "scripts"
        internal = scripts / "internal"
        internal.mkdir(parents=True)
        for name in ("shotstopper_cli.sh", "shotstopper_board.sh"):
            shutil.copy2(ROOT / "scripts" / name, scripts / name)
        shutil.copy2(INTERNAL / "firmware-idf", internal / "firmware-idf")
        if store is not None:
            (root / ".shotstopper").write_text(store)
        log = root / "children.log"
        for child in ("build-idf", "flash-idf", "ota-idf", "monitor-idf"):
            target = internal / child
            target.write_text(
                f'#!/bin/sh\nprintf "%s:%s\\n" "${{0##*/}}" "$*" >>"{log}"\n'
                + ("exit 9\n" if child == fail else ""))
            target.chmod(0o755)
        env = os.environ.copy()
        env.update(SS_CLI_ROOT=str(root), SHOTSTOPPER_NONINTERACTIVE="1",
                   SHOTSTOPPER_DEVICE_PASSWORD="test-only")
        env.pop("SHOTSTOPPER_FLAGS", None)
        result = subprocess.run(
            [str(internal / "firmware-idf"), *stages, "--", *args],
            cwd=root, env=env, capture_output=True, text=True)
        children = log.read_text().splitlines() if log.exists() else []
        return result, children


def stubbed_dispatcher(stages: tuple[str, ...], args: list[str], fail: str = "",
                       expected: int = 0, store: str | None = None) -> list[str]:
    result, children = dispatched(stages, args, fail, store)
    assert result.returncode == expected, (stages, result.stdout, result.stderr)
    return children


profile_args = ["--hardware", "esp32-s3-relay-x1-speaker",
                "--machine", "rancilio-silvia-pro-x"]
flash_monitor = stubbed_dispatcher(
    ("build", "flash", "monitor"),
    [*profile_args, "--port", "/dev/null", "--speed", "115200",
     "--webui-language", "EN_us", "--erase-all", "--jtag"])
assert [line.split(":", 1)[0] for line in flash_monitor] == [
    "build-idf", "flash-idf", "monitor-idf"]
assert "--webui-language en-us" in flash_monitor[0]
assert "--jtag" in flash_monitor[0]
assert "--erase-all" in flash_monitor[1]
assert "--speed 115200" in flash_monitor[2]

legacy_jtag_flags = stubbed_dispatcher(
    ("build", "flash", "monitor"),
    [*profile_args, "--port", "/dev/null", "--speed", "115200",
     "--webui-language", "EN_us", "--flags", "-DSHOT_STOPPER_ENABLE_JTAG=1"])
assert [line.split(":", 1)[0] for line in legacy_jtag_flags] == [
    "build-idf", "flash-idf", "monitor-idf"]

blocked, blocked_children = dispatched(("build", "flash", "monitor"), [])
assert blocked.returncode == 2 and "--jtag" in blocked.stderr and \
    not blocked_children, (blocked.returncode, blocked.stderr)
build_only = stubbed_dispatcher(("build",), [*profile_args, "--webui-language", "EN_us"])
assert [line.split(":", 1)[0] for line in build_only] == ["build-idf"]
flash_monitor_only = stubbed_dispatcher(
    ("flash", "monitor"),
    ["--arch", "n16r8", "--port", "/dev/null", "--speed", "115200"])
assert [line.split(":", 1)[0] for line in flash_monitor_only] == [
    "flash-idf", "monitor-idf"]

store_jtag = stubbed_dispatcher(
    ("build", "flash", "monitor"),
    [*profile_args, "--port", "/dev/null", "--speed", "115200",
     "--webui-language", "EN_us"],
    store="flags=-DSHOT_STOPPER_ENABLE_JTAG=1\n")
assert [line.split(":", 1)[0] for line in store_jtag] == [
    "build-idf", "flash-idf", "monitor-idf"]
store_plain, store_plain_children = dispatched(
    ("build", "flash", "monitor"),
    [*profile_args, "--port", "/dev/null", "--speed", "115200"],
    store="flags=-DCUSTOM=1\n")
assert store_plain.returncode == 2 and "--jtag" in store_plain.stderr and \
    not store_plain_children, (store_plain.returncode, store_plain.stderr)

ota_monitor = stubbed_dispatcher(
    ("build", "ota", "monitor"),
    [*profile_args, "--host", "127.0.0.1", "--port", "/dev/null",
     "--speed", "115200", "--yes", "--wait-for-confirmation", "--jtag"])
assert [line.split(":", 1)[0] for line in ota_monitor] == [
    "build-idf", "ota-idf", "monitor-idf"]
assert "--yes --wait-for-confirmation" in ota_monitor[1]
assert "--jtag" in ota_monitor[0]
assert "test-only" not in "\n".join(ota_monitor)

stopped = stubbed_dispatcher(
    ("build", "flash"), [*profile_args, "--port", "/dev/null"],
    fail="build-idf", expected=9)
assert [line.split(":", 1)[0] for line in stopped] == ["build-idf"]

no_yes = subprocess.run(
    [str(INTERNAL / "firmware-idf"), "ota", "--", "--arch", "n16r8"],
    cwd=ROOT, env={**os.environ, "SHOTSTOPPER_NONINTERACTIVE": "1"},
    capture_output=True, text=True)
assert no_yes.returncode == 2 and "requires --yes" in no_yes.stderr

for removed in ("b-idf", "f-idf", "m-idf", "o-idf", "s-idf", "bf-idf",
                "bfm-idf", "bo-idf", "bsfm-idf", "build-idf", "flash-idf",
                "monitor-idf", "ota-idf"):
    assert not (ROOT / "scripts" / removed).exists(), \
        f"obsolete public script remains: {removed}"


def idf_activation(active_version: str | None, active_python: bool = True):
    with tempfile.TemporaryDirectory(prefix="shotstopper-idf-") as temporary:
        root = Path(temporary)
        active = root / "active"
        fallback = root / "esp/esp-idf-v6.1"
        active_env = root / "active-python"
        fallback_env = root / "fallback-python"
        for idf, env, version in ((active, active_env, active_version),
                                  (fallback, fallback_env, "6.1")):
            (idf / "tools").mkdir(parents=True)
            (env / "bin").mkdir(parents=True)
            (env / "bin/python").write_text("#!/bin/sh\nexit 0\n")
            (env / "bin/python").chmod(0o755)
            (idf / "tools/idf.py").write_text(
                f"#!/bin/sh\necho 'ESP-IDF v{version}'\n")
            (idf / "tools/idf.py").chmod(0o755)
        (active / "export.sh").write_text("return 99\n")
        (fallback / "export.sh").write_text(
            f'export IDF_PATH="{fallback}"\n'
            f'export IDF_PYTHON_ENV_PATH="{fallback_env}"\n'
            f'export PATH="{fallback}/tools:$PATH"\n')
        if not active_python:
            (active_env / "bin/python").unlink()
        env = os.environ.copy()
        env.update(HOME=str(root), PATH="/usr/bin:/bin")
        for name in ("IDF_PATH", "IDF_PYTHON_ENV_PATH", "ESP_PYTHON",
                     "ESP_IDF_VERSION", "IDF_DEACTIVATE_FILE_PATH"):
            env.pop(name, None)
        if active_version is not None:
            env.update(PATH=f"{active}/tools:{env['PATH']}", IDF_PATH=str(active),
                       IDF_PYTHON_ENV_PATH=str(active_env))
        command = (
            f'source "{ROOT / "scripts/shotstopper_idf.sh"}"; '
            'SS_IDF_QUIET=1; ss_idf_source; '
            'printf "%s|%s|%s" "$IDF_PATH" "$IDF_PYTHON_ENV_PATH" '
            '"$(command -v idf.py)"')
        return subprocess.run(["bash", "-c", command], env=env,
                              capture_output=True, text=True), active, fallback


active_idf, active_root, _ = idf_activation("6.1")
active_values = active_idf.stdout.split("|")
assert active_idf.returncode == 0 and active_values == [
    str(active_root), str(active_root.parent / "active-python"),
    str(active_root / "tools/idf.py")], active_idf.stderr
for version, has_python in (("6.0", True), ("6.1", False)):
    fallback_idf, _, fallback_root = idf_activation(version, has_python)
    fallback_values = fallback_idf.stdout.split("|")
    assert fallback_idf.returncode == 0 and fallback_values == [
        str(fallback_root), str(fallback_root.parent.parent / "fallback-python"),
        str(fallback_root / "tools/idf.py")], fallback_idf.stderr
legacy_idf, _, fallback_root = idf_activation(None)
legacy_values = legacy_idf.stdout.split("|")
assert legacy_idf.returncode == 0 and legacy_values == [
    str(fallback_root), str(fallback_root.parent.parent / "fallback-python"),
    str(fallback_root / "tools/idf.py")], legacy_idf.stderr


def flash_command(layout: str, *extra: str, arch: str = "n8r4"):
    with tempfile.TemporaryDirectory(prefix="shotstopper-flash-") as temporary:
        root = Path(temporary)
        idf = root / "fake-idf"
        tools = idf / "tools"
        python_env = root / "python-env/bin"
        build = root / f"build-idf/{arch}"
        partition_tool = idf / "components/partition_table/gen_esp32part.py"
        for directory in (tools, python_env, build, partition_tool.parent,
                          root / "idf"):
            directory.mkdir(parents=True, exist_ok=True)
        log = root / "commands.log"
        image = root / "external.bin"
        image.write_bytes(b"external")
        (build / "shotstopper.bin").write_bytes(b"project")
        (build / "flash_args").write_text("0x0 bootloader.bin\n")
        (idf / "export.sh").write_text("return 99\n")
        (python_env / "python").write_text("#!/bin/sh\nexit 0\n")
        (python_env / "python").chmod(0o755)
        partition_tool.write_text(
            "from pathlib import Path\nimport os,sys\n"
            "offset = '0x680000' if os.environ['EXPECTED_ARCH'] == 'n8r4' "
            "else '0x620000'\n"
            "if os.environ['FLASH_LAYOUT'] == 'wrong': "
            "offset = '0x620000' if offset == '0x680000' else '0x680000'\n"
            "part_type = 'app' if os.environ['FLASH_LAYOUT'] == 'type' else 'data'\n"
            "subtype = '65' if os.environ['FLASH_LAYOUT'] == 'subtype' else '64'\n"
            "size = '36K' if os.environ['FLASH_LAYOUT'] == 'size' else '40K'\n"
            "curve = '' if os.environ['FLASH_LAYOUT'] == 'missing' else "
            "f'shotcurve,{part_type},{subtype},{offset},{size}\\n'\n"
            "Path(sys.argv[-1]).write_text('nvs,data,nvs,0x9000,0x15000\\n'"
            "+'app0,app,ota_0,0x20000,0x330000\\n'+curve)\n")
        (tools / "idf.py").write_text(
            "#!/bin/sh\n"
            f'printf "idf:%s\\n" "$*" >>"{log}"\n'
            "case \" $* \" in *' --version '*) echo 'ESP-IDF v6.1';; esac\n")
        (tools / "node").write_text("#!/bin/sh\nexit 0\n")
        (tools / "python").write_text(f'#!/bin/sh\nexec "{sys.executable}" "$@"\n')
        (tools / "esptool.py").write_text(
            "#!/usr/bin/env python3\nimport os, pathlib, sys\n"
            f"open({str(log)!r}, 'a').write('esptool:' + ' '.join(sys.argv[1:]) + '\\n')\n"
            "if 'read_flash' in sys.argv:\n"
            "    if os.environ['FLASH_LAYOUT'] == 'unknown': sys.exit(1)\n"
            "    fill = 255 if os.environ['FLASH_LAYOUT'] == 'blank' else 0\n"
            "    pathlib.Path(sys.argv[-1]).write_bytes(bytes([fill]) * 4096)\n")
        for executable in (tools / "idf.py", tools / "node", tools / "python",
                           tools / "esptool.py"):
            executable.chmod(0o755)
        env = os.environ.copy()
        env.update(SS_CLI_ROOT=str(root), SHOTSTOPPER_NONINTERACTIVE="1",
                   IDF_PATH=str(idf), IDF_PYTHON_ENV_PATH=str(python_env.parent),
                   FLASH_LAYOUT=layout, EXPECTED_ARCH=arch, TMPDIR=str(root),
                   PATH=f"{tools}:/usr/bin:/bin")
        args = [str(INTERNAL / "flash-idf"), "--port", "/dev/null",
                "--arch", arch, *extra]
        if "--image" in extra:
            args[args.index("--image") + 1] = str(image)
        result = subprocess.run(args, env=env, cwd=ROOT, capture_output=True, text=True)
        return result, log.read_text().splitlines()


blank_result, blank_flash = flash_command("blank")
assert blank_result.returncode == 0 and \
    any("write_flash @flash_args" in line for line in blank_flash), blank_result.stderr
erased_result, erased_flash = flash_command("missing", "--erase-all")
assert erased_result.returncode == 0 and \
    any("erase_flash" in line for line in erased_flash) and \
    any("write_flash @flash_args" in line for line in erased_flash), erased_flash
present_result, present_flash = flash_command("present")
assert present_result.returncode == 0 and \
    any("write_flash @flash_args" in line for line in present_flash) and \
    not any(" flash" in line for line in present_flash if line.startswith("idf:")), present_flash
present_n16_result, _ = flash_command("present", arch="n16r8")
assert present_n16_result.returncode == 0, present_n16_result.stderr
unchecked_result, unchecked_flash = flash_command("present", "--no-check")
assert unchecked_result.returncode == 0 and \
    any("write_flash @flash_args" in line for line in unchecked_flash) and \
    not any(" flash" in line for line in unchecked_flash if line.startswith("idf:")), \
    unchecked_flash
external_result, external_flash = flash_command("present", "--image", "placeholder")
assert external_result.returncode == 0 and \
    any("write_flash 0x20000" in line for line in external_flash) and \
    not any(" flash" in line for line in external_flash if line.startswith("idf:")), \
    external_flash
for incompatible_layout in ("missing", "wrong", "type", "subtype", "size"):
    for extra in ((), ("--image", "placeholder")):
        rejected, commands = flash_command(incompatible_layout, *extra)
        assert rejected.returncode == 1 and "required n8r4 shotcurve" in rejected.stderr, \
            (incompatible_layout, extra, rejected.stderr)
        assert not any("write_flash" in line for line in commands), commands
flash_language = subprocess.run(
    [str(INTERNAL / "flash-idf"), "--webui-language", "en"],
    cwd=ROOT, capture_output=True, text=True)
assert flash_language.returncode == 2 and "build-only" in flash_language.stderr

scripts_text = "\n".join(
    path.read_text(errors="replace") for path in (ROOT / "scripts").rglob("*")
    if path.is_file())
idf_helpers = (ROOT / "scripts/shotstopper_idf.sh").read_text()
assert 'IDF_DEFAULT_HOME="${HOME}/esp/esp-idf-v6.1"' in idf_helpers, \
    "official SDK discovery must use the versioned ESP-IDF 6.1 checkout"
assert "python@3.12" not in idf_helpers, \
    "ESP-IDF 6.1 must activate the Python environment created by its installer"
assert '. "${idf_root}/export.sh" >/dev/null' in idf_helpers, \
    "non-interactive builds must not print ESP-IDF shell-completion warnings"


def verify_production_profile(arch: str, selectors: list[str]):
    with tempfile.NamedTemporaryFile(mode="w", prefix="shotstopper-sdkconfig-",
                                     delete=False) as fixture:
        fixture.write("\n".join(selectors) + "\n")
        fixture_path = fixture.name
    try:
        return subprocess.run(
            ["bash", "-c",
             f'source "{ROOT / "scripts/shotstopper_idf.sh"}"; '
             'ss_idf_verify_production_profile "$1" "$2"',
             "profile-test", arch, fixture_path],
            cwd=ROOT, text=True, capture_output=True)
    finally:
        Path(fixture_path).unlink()


profile_common = [
    'CONFIG_PARTITION_TABLE_CUSTOM=y',
    'CONFIG_ESPTOOLPY_FLASHMODE_DIO=y',
    'CONFIG_ESPTOOLPY_FLASHMODE="dio"',
    'CONFIG_ESPTOOLPY_FLASHMODE_VAL=3',
    'CONFIG_ESPTOOLPY_FLASHFREQ_80M=y',
    'CONFIG_ESPTOOLPY_FLASHFREQ="80m"',
    'CONFIG_SPIRAM_SPEED_80M=y',
    'CONFIG_SPIRAM_SPEED=80',
    'CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768',
    'CONFIG_MMU_PAGE_SIZE_64KB=y',
    'CONFIG_MMU_PAGE_SIZE=0x10000',
    'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y',
    'CONFIG_BOOTLOADER_WDT_ENABLE=y',
    'CONFIG_BOOTLOADER_WDT_TIME_MS=9000',
    'CONFIG_ESP_INT_WDT=y',
    'CONFIG_ESP_INT_WDT_TIMEOUT_MS=800',
    'CONFIG_ESP_TASK_WDT_EN=y',
    'CONFIG_ESP_TASK_WDT_INIT=y',
    'CONFIG_ESP_TASK_WDT_PANIC=y',
    'CONFIG_ESP_TASK_WDT_TIMEOUT_S=5',
    'CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y',
    'CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=y',
    'CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y',
    'CONFIG_ESP_SYSTEM_PANIC_REBOOT_DELAY_SECONDS=0',
    'CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM=y',
]
profile_arch = {
    "n8r4": ['CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y',
              'CONFIG_ESPTOOLPY_FLASHSIZE="8MB"',
              'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-n8r4.csv"',
              'CONFIG_PARTITION_TABLE_FILENAME="partitions-n8r4.csv"',
              'CONFIG_SPIRAM_MODE_QUAD=y'],
    "n16r8": ['CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y',
               'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
               'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-n16r8.csv"',
               'CONFIG_PARTITION_TABLE_FILENAME="partitions-n16r8.csv"',
               'CONFIG_SPIRAM_MODE_OCT=y'],
}
for arch, arch_selectors in profile_arch.items():
    selectors = profile_common + arch_selectors
    assert verify_production_profile(arch, selectors).returncode == 0
    for selector in selectors:
        result = verify_production_profile(
            arch, [item for item in selectors if item != selector])
        assert result.returncode == 1 and "Production profile mismatch" in result.stderr
    opposite = "OCT" if arch == "n8r4" else "QUAD"
    assert verify_production_profile(
        arch, selectors + [f"CONFIG_SPIRAM_MODE_{opposite}=y"]).returncode == 1

durable_stores = (ROOT / "src/ShotStopperDurableStores.h").read_text()
network_reset = durable_stores.split("bool resetPersistedNetworkAccess", 1)[1].split(
    "bool resetAllDurableStores", 1)[0]
assert re.findall(r"^\s+PersistedSettings\s+(\w+);", network_reset, re.MULTILINE) == [
    "candidate"]
scale_worker = (ROOT / "src/ShotStopperScaleWorker.cpp").read_text()
cleanup = scale_worker.split("if (!runtimeReady) {", 1)[1].split(
    "scaleWorkerStartupFinished.store", 1)[0]
cleanup_order = [cleanup.index(token) for token in (
    "feedCurrentTaskWatchdog()", "shotStopperBleRuntimeStop(BLE_STACK_STOP_WAIT_MS)",
    "esp_task_wdt_delete(nullptr)", "scaleWorkerTaskHandle.store(nullptr")]
assert cleanup_order == sorted(cleanup_order)
assert "if (runtimeStopped)" in cleanup
assert "std::atomic<TaskHandle_t> scaleWorkerTaskHandle" in scale_worker
assert "TaskLockGuard lock(scaleWorkerTaskHandleMutex)" in cleanup
reset_history = (ROOT / "src/ShotStopperResetHistoryStore.h").read_text()
checkpoint = reset_history.split("bool persistResetUptimeCheckpoint", 1)[1].split(
    "bool clearPersistedResetHistory", 1)[0]
assert checkpoint.count("resetHistoryStoreLastCheckpointMs.load") == 2
assert checkpoint.index("resetHistoryStoreLastCheckpointMs.load") < checkpoint.index(
    "tryLockFlashIo")
buzzer = (ROOT / "src/ShotStopperBuzzer.h").read_text()
assert "TaskMutex mutex" in buzzer and "portMUX" not in buzzer
network_header = (ROOT / "src/ShotStopperNetwork.h").read_text()
assert "std::atomic<uint32_t> lastTaskProgressAtMs_" in network_header
nimble_client = (ROOT / "libraries/EspressoScaleBLE/src/EspressoScaleBLENimble.cpp").read_text()
for capacity in ("kRxFrameCount", "kCriticalEventCount", "kEventCount"):
    assert f"drained < {capacity}" in nimble_client
shot_curve = (ROOT / "src/ShotStopperShotCurve.h").read_text()
assert "flashIoLockTimeouts() == lockTimeoutsBefore" in shot_curve
assert "(void)load();" in shot_curve
persistence_runtime = (ROOT / "src/persistence/ShotStopperCommandPersistence.inc").read_text()
checkpoint = persistence_runtime.split("SETTINGS_PERSIST_IDLE_WAIT_MS", 1)[1].split(
    "yieldSettingsNvs", 1)[0]
for gate in ("copyControlGate(control)", "control.activeCycle", "control.relayClosed",
             "control.machineRunning", "control.physicalActivatorOn", "link.connecting"):
    assert gate in checkpoint
assert checkpoint.index("feedOrTripCurrentTaskWatchdog()") < checkpoint.index(
    "persistResetUptimeCheckpoint")
ota_service = (ROOT / "src/network/ShotStopperNetworkOta.inc").read_text()
assert "control.bootState == BootState::READY && startupComplete_" in ota_service
ota_boot_call = ota_service.split("ota.serviceBoot", 1)[1].split(
    "OTA_CONFIRM_MIN_UPTIME_MS", 1)[0]
for gate in ("control.activeCycle", "control.machineRunning",
             "control.physicalActivatorOn"):
    assert gate in ota_boot_call
control_gate = (ROOT / "src/diagnostics/ShotStopperDiagnostics.inc").read_text().split(
    "void publishControlGate()", 1)[1].split("void publishControlStatus()", 1)[0]
for source in ("bootCapabilities", "criticalTaskWatchdogFaulted()",
               "relay.resetRecoveryRequired", "RelaySafetyState::LOCKOUT"):
    assert source in control_gate
assert "next.bootState = effectiveBoot.state()" in control_gate
assert "constexpr uint32_t FLASH_IO_LOCK_TIMEOUT_MS = 3000" in \
    (ROOT / "src/ShotStopperFlashIoScratch.h").read_text()
assert r"component_validation\.cmake" in idf_helpers and \
    'idf "esp_wifi/"' in idf_helpers and 'idf "wpa_supplicant/"' in idf_helpers, \
    "only the known ESP-IDF 6.1 Wi-Fi component warnings may be filtered"
assert 'build 2>&1 | ss_idf_filter_output' in \
    (INTERNAL / "build-idf").read_text(), \
    "the firmware build must use the scoped external-warning filter"
for removed_alias in ("b", "bf", "bfm", "bo", "bsfm", "build", "f", "flash",
                      "iwyu", "m", "monitor", "o", "ota", "s", "static",
                      "static-tidy", "warnings"):
    assert not (ROOT / "scripts" / removed_alias).exists(), \
        f"legacy Arduino-era alias remains: {removed_alias}"
assert not re.search(r"--(?:token|password)\s+['\"]", scripts_text), \
    "credentials must not be forwarded in argv"
for analyzer in ("warnings-idf", "gcc_analyzer"):
    assert "./scripts/internal/build-idf" in (ROOT / "scripts" / analyzer).read_text()
cppcheck_suppressions = (ROOT / "scripts/cppcheck-suppressions.txt").read_text()
assert "**" not in cppcheck_suppressions, \
    "Cppcheck suppression globs must use a single '*' wildcard"
assert "*:*/esp-idf*/components/*" in cppcheck_suppressions, \
    "Cppcheck must suppress dependencies from versioned ESP-IDF checkouts"
tests_text = "\n".join(path.read_text(errors="replace")
                       for path in (ROOT / "src/tests").iterdir() if path.is_file())
assert "npm" + " install" not in tests_text and "npm" + " ci" not in tests_text, \
    "tests must not install dependencies"

workflow = (ROOT / ".github/workflows/validation.yml").read_text()
for job in ("classify", "fast", "host", "idf", "gate"):
    assert f"  {job}:\n" in workflow, f"CI job missing: {job}"
assert "  analysis:\n" not in workflow, "analysis must share the IDF build workspace"
assert "actions/download-artifact" not in workflow, \
    "the compilation database must not cross job boundaries"
host_job = workflow.split("  host:\n", 1)[1].split("\n  idf:\n", 1)[0]
assert "libcjson-dev" in host_job, \
    "host CI must install the cJSON development files"
idf_job = workflow.split("  idf:\n", 1)[1].split("\n  gate:\n", 1)[0]
assert "cppcheck" in idf_job, "IDF CI must install Cppcheck"
assert "github.event_name != 'pull_request'" in idf_job, \
    "main, scheduled, and manual CI runs must publish firmware profiles"
for profile_row in (
        "{name: linea-micra, hardware: esp32-s3-relay-x1-speaker, machine: la-marzocco-linea-micra}",
        "{name: silvia-pro-x, hardware: esp32-s3-relay-x1-speaker, machine: rancilio-silvia-pro-x}",
        "{name: silvia-pro-x-reed, hardware: esp32-s3-relay-x1-speaker-reed, machine: rancilio-silvia-pro-x-reed}"):
    assert profile_row in idf_job, f"IDF CI profile missing: {profile_row}"
for disabled_flag in ("SHOT_STOPPER_ENABLE_JTAG=0",
                      "SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=0"):
    assert disabled_flag in idf_job, f"CI production flag missing: {disabled_flag}"
ota_name = "shotstopper-ota-${{ matrix.name }}-jtag-off-remote-off"
assert f"name: {ota_name}" in idf_job
assert (f"build-idf/${{{{ matrix.hardware }}}}--${{{{ matrix.machine }}}}/"
        f"{ota_name}.bin") in idf_job
assert workflow.count("actions/upload-artifact") == 4, \
    "classification, fast, host, and IDF jobs must publish diagnostic artifacts"
for artifact_name in ("validation-classify", "validation-fast", "validation-host"):
    assert f"name: {artifact_name}" in workflow
assert workflow.count("if: always()") == 5, \
    "every artifact upload and the final gate must run after failures"
assert "shotstopper.elf" not in workflow, \
    "non-portable firmware ELF files must not be published"
for diagnostic_path in ("ci-results/idf/", "reports/", "artifacts/runs/"):
    assert diagnostic_path in idf_job, \
        f"firmware artifacts must include available diagnostics: {diagnostic_path}"
build = idf_job.index("./scripts/dev build")
firmware_upload = idf_job.index("actions/upload-artifact")
cppcheck = idf_job.index("./scripts/dev analyze")
tidy = idf_job.index("./scripts/static-tidy-idf")
iwyu = idf_job.index("./scripts/iwyu-idf")
warnings = idf_job.index("./scripts/warnings-idf")
gcc_analyzer = idf_job.index("./scripts/gcc_analyzer")
assert build < cppcheck < tidy < warnings < gcc_analyzer < firmware_upload
assert cppcheck < iwyu < warnings
assert idf_job.count("set -o pipefail") == 6 and idf_job.count("tee ci-results/idf/") == 6, \
    "IDF command logs must be retained without masking failures"
assert "compile_commands.json" not in idf_job, \
    "compile commands are not a portable standalone artifact"
gate_job = workflow.split("  gate:\n", 1)[1]
assert "needs.analysis" not in gate_job and "ANALYSIS:" not in gate_job, \
    "the gate must use the combined IDF build and analysis result"
assert "EVENT_NAME: ${{ github.event_name }}" in gate_job
assert '"$EVENT_NAME" == pull_request' in gate_job, \
    "non-PR CI runs must fail when firmware artifacts cannot be built"
for use in re.findall(r"uses:\s*([^\s]+)", workflow):
    assert re.search(r"@[0-9a-f]{40}$", use), f"action is not SHA-pinned: {use}"
assert "cancel-in-progress: true" in workflow and "contents: read" in workflow
assert "scripts/flash" not in workflow and "scripts/ota-idf" not in workflow
template = (ROOT / ".github/PULL_REQUEST_TEMPLATE.md").read_text()
for field in ("Risk", "Safety invariants", "Validation", "HIL/manual evidence"):
    assert field in template, f"PR template field missing: {field}"
print(f"developer facade: {len(golden)} risk cases, 11 context areas, and script contracts passed")
