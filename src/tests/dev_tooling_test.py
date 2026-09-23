#!/usr/bin/env python3
"""Golden contracts for the developer facade."""

from __future__ import annotations

import json
import subprocess
from contextlib import contextmanager
from pathlib import Path
import os
import re
import csv
import io
import runpy
import shutil
from tempfile import mkdtemp
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
assert obsolete.returncode == 2 and "USB" in obsolete.stderr
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


for pipeline in (("build",), ("flash",), ("monitor",),
                 ("build", "flash"),
                 ("flash", "monitor"),
                 ("build", "flash", "monitor"),
                 ):
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
opt_forwarded = captured_firmware("build", "--os")
assert opt_forwarded["steps"][0][1][-2:] == ["--", "--os"]
# The repository default optimization level is -Os/SIZE: sdkconfig.defaults
# selects it and every script fallback agrees.
defaults_text = (ROOT / "idf" / "sdkconfig.defaults").read_text()
assert "CONFIG_COMPILER_OPTIMIZATION_SIZE=y" in defaults_text
assert "CONFIG_COMPILER_OPTIMIZATION_PERF=y" not in defaults_text
internal_build = (INTERNAL / "build-idf").read_text()
assert internal_build.count("SS_IDF_OPT_LEVEL_KCONFIG=CONFIG_COMPILER_OPTIMIZATION_SIZE") == 2
assert "SS_IDF_OPT_LEVEL_KCONFIG=CONFIG_COMPILER_OPTIMIZATION_PERF" in internal_build
idf_helper = (INTERNAL / ".." / "shotstopper_idf.sh").resolve().read_text()
assert idf_helper.count("-CONFIG_COMPILER_OPTIMIZATION_SIZE}") == 2
assert idf_helper.count("-CONFIG_COMPILER_OPTIMIZATION_PERF}") == 0
for level in ("--o0", "--og", "--o2", "--os"):
    rejected = run("flash", level)
    assert rejected.returncode == 2 and "does not apply" in rejected.stderr, level

for area in ("safety", "control", "machine", "scale", "ble", "network",
             "ota", "persistence", "web", "build", "tests"):
    result = run("context", area)
    assert result.returncode == 0 and area in result.stdout, (area, result.stderr)

dry_run = run("clean", "--dry-run")
assert dry_run.returncode == 0 and "build-host" in dry_run.stdout

# Parallel-agent hardening: gates must name changed paths outside an explicit
# scope instead of silently classifying only what was passed.
assert dev_module["uncovered_changes"]([]) == []
assert dev_module["uncovered_changes"](["."]) == []
assert (dev_module["uncovered_changes"](["./scripts/dev"])
        == dev_module["uncovered_changes"](["scripts/dev"]))

# Compact-mode failure hint: a failed gate must summarize the failing check
# and the likeliest cause instead of making the reader dig through the tail.
failed_line = dev_module["failed_check_line"]
hint = failed_line([{"name": "build-normal", "status": "failed"}],
                   ["==> build-normal: cmake --build build-host",
                    "src/X.h:37:15: error: something broke"], "failed")
assert hint == ("failed check: build-normal — src/X.h:37:15: error: "
                "something broke"), hint
assert "cmake: MISSING" in failed_line(
    [{"name": "doctor", "status": "failed"}], ["cmake: MISSING"],
    "missing_dependency")

# Per-variant build serialization: build-idf must re-exec through with-flock
# holding the variant lock, and the helper must be the portable fcntl holder.
with_flock = INTERNAL / "with-flock"
assert with_flock.is_file() and with_flock.stat().st_mode & 0o111
assert "fcntl.flock" in with_flock.read_text()
assert "set_inheritable" in with_flock.read_text(), (
    "PEP 446 close-on-exec would release the lock before the build starts")
assert "SS_BUILD_LOCKED" in internal_build and "with-flock" in internal_build
assert ".locks/" in internal_build and "SHOTSTOPPER_VARIANT.lock" in internal_build

# Serialization actually blocks: a held lock must delay a second holder.
import time as _time
with tempfile.TemporaryDirectory(prefix="ss-flock-probe-") as flock_dir:
    lock_path = Path(flock_dir) / "probe.lock"
    first = subprocess.Popen(
        [sys.executable, str(with_flock), str(lock_path), "sleep", "2"])
    _time.sleep(0.5)
    started = _time.monotonic()
    second = subprocess.run(
        [sys.executable, str(with_flock), str(lock_path), "true"])
    elapsed = _time.monotonic() - started
    first.wait()
    assert first.returncode == 0 and second.returncode == 0
    assert elapsed >= 1.0, f"lock did not serialize (waited {elapsed:.2f}s)"


@contextmanager
def idf_doctor_fixture(with_install: bool, with_venv: bool):
    """Create a sandbox HOME with an optional ESP-IDF checkout and python_env."""
    with tempfile.TemporaryDirectory(prefix="shotstopper-doctor-idf-") as temporary:
        root = Path(temporary)
        fallback = root / "esp/esp-idf-v6.1"
        installed_env = root / ".espressif/python_env/idf6.1_py3.14_env"
        missing_env = root / ".espressif/python_env/idf6.1_py3.11_env"
        if with_install:
            (fallback / "components/esp_common/include").mkdir(parents=True)
            (fallback / "export.sh").write_text("#!/bin/sh\n")
            (fallback / "components/esp_common/include/esp_idf_version.h").write_text(
                "#define ESP_IDF_VERSION_MAJOR 6\n"
                "#define ESP_IDF_VERSION_MINOR 1\n")
        if with_venv:
            (installed_env / "bin").mkdir(parents=True, exist_ok=True)
            (installed_env / "bin/python").write_text("#!/bin/sh\nexit 0\n")
            (installed_env / "bin/python").chmod(0o755)
            (installed_env / "idf_version.txt").write_text("6.1")
        missing_env.mkdir(parents=True, exist_ok=True)
        yield root, fallback, installed_env, missing_env


def idf_environment_probe(root: Path, extra_env: dict[str, str] | None = None):
    env = os.environ.copy()
    env.update(HOME=str(root), SS_CLI_ROOT=str(root))
    for name in ("IDF_PATH", "IDF_PYTHON_ENV_PATH", "ESP_PYTHON",
                 "ESP_IDF_VERSION", "IDF_DEACTIVATE_FILE_PATH"):
        env.pop(name, None)
    env.update(extra_env or {})
    command = (
        f'source "{ROOT / "scripts/shotstopper_board.sh"}"; '
        f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
        f'source "{ROOT / "scripts/shotstopper_idf.sh"}"; '
        'ss_idf_environment_report')
    return subprocess.run(["bash", "-c", command], env=env, cwd=ROOT,
                          capture_output=True, text=True)


def doctor_with_probe_stdout(probe_stdout: str) -> list[str]:
    """Render doctor output for a canned environment probe result."""
    captured = {}

    def capture(command, risk, verbosity, lines, code=0, check="contract"):
        captured.update(lines=list(lines))
        return 0

    class FakeSubprocess:
        @staticmethod
        def run(*args, **kwargs):
            return subprocess.CompletedProcess(args=[], returncode=0,
                                               stdout=probe_stdout, stderr="")

    main = dev_module["main"]
    with patch.dict(main.__globals__, {"subprocess": FakeSubprocess,
                                       "record": capture}), \
            patch.object(sys, "argv", [str(DEV), "doctor"]):
        assert main() == 0
    return captured["lines"]


for with_install, with_venv, extra, expected_idf, expected_venv, mismatched in (
        (True, True, None, True, "auto-selected installed venv", False),
        (True, True, {"IDF_PYTHON_ENV_PATH": "ACTIVE"}, True, "active environment", False),
        (True, False, None, True, "resolved by export.sh at build time", False),
        (True, True, {"IDF_PYTHON_ENV_PATH": "STALE"}, True,
         "auto-selected installed venv", True),
        (False, True, None, False, None, False)):
    with idf_doctor_fixture(with_install, with_venv) as (root, fallback,
                                                         installed_env,
                                                         missing_env):
        extra_env = {name: (str(installed_env) if value == "ACTIVE" else
                            str(missing_env) if value == "STALE" else value)
                     for name, value in (extra or {}).items()}
        probe = idf_environment_probe(root, extra_env)
        assert (probe.returncode == 0) == expected_idf, probe.stderr
        lines = doctor_with_probe_stdout(probe.stdout)
        if expected_idf:
            assert any(line == f"ESP-IDF: {fallback}" +
                       (" (venv mismatch)" if mismatched else "")
                       for line in lines), lines
            assert any(expected_venv in line and
                       line.startswith("IDF Python environment: ")
                       for line in lines), lines
            assert any("venv mismatch" in line for line in lines) == mismatched, lines
        else:
            assert "ESP-IDF: not found; install 6.1.x per docs/BUILD.md" in lines, lines

live_doctor = run("doctor")
assert live_doctor.returncode == 0 and "ESP-IDF:" in live_doctor.stdout, \
    live_doctor.stderr

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
                 "shotcurve": (0x620000, 0xE000),
                 "shotlog": (0x62E000, 0x8000),
                 "history": (0x636000, 0x8000),
                 "ffat": (0x63E000, 0x9B2000),
                 "coredump": (0xFF0000, 0x10000)},
    },
    "partitions-n8r4.csv": {
        "flash": 0x800000,
        "rows": {"nvs": (0x9000, 0x15000), "otadata": (0x1E000, 0x2000),
                 "app0": (0x20000, 0x330000), "app1": (0x350000, 0x330000),
                 "shotcurve": (0x680000, 0xE000),
                 "shotlog": (0x68E000, 0x8000),
                 "history": (0x696000, 0x8000),
                 "spiffs": (0x69E000, 0x152000),
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
    assert rows["shotcurve"][1] == 56 * 1024, "shot-curve partition changed"
    assert rows["shotlog"][1] == 32 * 1024, "shot-log partition changed"
    assert rows["history"][1] == 32 * 1024, "activation-history partition changed"

flash_idf = (INTERNAL / "flash-idf").read_text()
for required in ("read-flash 0x8000 0x1000", "installed_nvs_bytes != 0x15000",
                 "installed_layout=blank", "installed_shotcurve_row",
                 "required_shotcurve_offset=0x680000", "0x620000", "erase-flash",
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


def static_paths_probe(*args: str, output_dir: str | None = None,
                       build_dir: str | None = None,
                       databases: tuple[str, ...] = (),
                       empty_variants: tuple[str, ...] = ()) -> subprocess.CompletedProcess[str]:
    """Resolve the shared static-analysis path set in a sandboxed CLI root."""
    with tempfile.TemporaryDirectory(prefix="shotstopper-static-paths-") as temporary:
        root = Path(temporary)
        env = os.environ.copy()
        env.update(SS_CLI_ROOT=str(root), SHOTSTOPPER_NONINTERACTIVE="1",
                   HOME=str(root))
        for name in ("IDF_PATH", "IDF_PYTHON_ENV_PATH"):
            env.pop(name, None)
        flags = ["--arch", "n16r8"]
        if build_dir is not None:
            flags += ["--build-dir", build_dir]
        if output_dir is not None:
            flags += ["--output-dir", output_dir]
        for database, stamp in databases:
            target = root / database
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("[]")
            os.utime(target, (stamp, stamp))
        for variant in empty_variants:
            (root / variant).mkdir(parents=True, exist_ok=True)
        command = (
            f'source "{ROOT / "scripts/shotstopper_board.sh"}"; '
            f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
            f'source "{ROOT / "scripts/shotstopper_idf.sh"}"; '
            'ss_cli_parse "$@" || exit $?; '
            'shotstopper_resolve_board "$(ss_get arch)" || exit 2; '
            'ss_idf_resolve_paths; ss_idf_static_paths reports/static-tidy || exit $?; '
            'printf "build_dir=%s\\nbuild_path=%s\\noutput_dir=%s\\n" '
            '"$build_dir" "$build_path" "$output_dir"')
        return subprocess.run(["bash", "-c", command, "probe", *flags, *args],
                              env=env, cwd=ROOT, capture_output=True, text=True)


discovered = static_paths_probe(
    databases=(("build-idf/n16r8/compile_commands.json", 1000.0),))
assert discovered.returncode == 0, (discovered.returncode, discovered.stdout,
                                    discovered.stderr)
fields = dict(line.split("=", 1) for line in discovered.stdout.splitlines())
assert fields["build_dir"].endswith("build-idf/n16r8"), discovered.stdout
assert fields["build_path"].endswith("build-idf/n16r8"), discovered.stdout

newer = static_paths_probe(
    databases=(("build-idf/older--pair/compile_commands.json", 1000.0),
               ("build-idf/newer--pair/compile_commands.json", 2000.0)))
fields = dict(line.split("=", 1) for line in newer.stdout.splitlines())
assert newer.returncode == 0 and \
    fields["build_dir"].endswith("build-idf/newer--pair"), newer.stdout
spaced = static_paths_probe(
    databases=(("build-idf/my variant--pair/compile_commands.json", 3000.0),))
fields = dict(line.split("=", 1) for line in spaced.stdout.splitlines())
assert spaced.returncode == 0 and \
    fields["build_dir"].endswith("build-idf/my variant--pair"), spaced.stdout
ignored_empty = static_paths_probe(
    databases=(("build-idf/older--pair/compile_commands.json", 1000.0),),
    empty_variants=("build-idf/empty--pair",))
fields = dict(line.split("=", 1) for line in ignored_empty.stdout.splitlines())
assert ignored_empty.returncode == 0 and \
    fields["build_dir"].endswith("build-idf/older--pair"), ignored_empty.stdout
explicit = static_paths_probe(
    build_dir="build-idf/older--pair", output_dir="reports/custom",
    databases=(("build-idf/older--pair/compile_commands.json", 1000.0),
               ("build-idf/newer--pair/compile_commands.json", 2000.0)))
assert explicit.returncode == 0, (explicit.returncode, explicit.stdout, explicit.stderr)
fields = dict(line.split("=", 1) for line in explicit.stdout.splitlines())
assert fields["build_dir"] == "build-idf/older--pair", explicit.stdout
assert fields["build_path"].endswith("build-idf/older--pair"), explicit.stdout
assert fields["output_dir"] == "reports/custom", explicit.stdout

missing = static_paths_probe()
assert missing.returncode == 1, (missing.returncode, missing.stdout, missing.stderr)
assert "build-idf/n16r8/compile_commands.json does not exist." in missing.stderr, \
    missing.stderr

for unsafe in ("/tmp/escape", "reports/../.."):
    rejected = static_paths_probe(output_dir=unsafe)
    assert rejected.returncode == 2, (unsafe, rejected.returncode,
                                      rejected.stdout, rejected.stderr)
    assert "relative path inside the repository" in rejected.stderr, rejected.stderr


def fake_database_sandbox() -> tuple[Path, Path, tempfile.TemporaryDirectory]:
    """Sandbox holding a fake compile database mirroring the project manifest."""
    sandbox = tempfile.TemporaryDirectory(prefix="shotstopper-static-idf-")
    root = Path(sandbox.name)
    build = root / "build-idf" / "n16r8"
    (build / "generated").mkdir(parents=True)
    (build / "generated" / "build-profile.json").write_text(
        '{"machine_integration": "none"}\n')
    # The real audit enumerates the manifest for the integration the
    # build profile selects; mirror that selection for "none".
    expected = []
    for raw in (ROOT / "scripts/project-translation-units.txt").read_text().splitlines():
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        relative, separator, selector = entry.partition("|")
        if separator and selector.strip().partition("=")[2] != "none":
            continue
        expected.append(relative.strip())
    (build / "compile_commands.json").write_text(json.dumps(
        [{"directory": str(ROOT), "file": str(ROOT / relative),
          "arguments": ["cc", relative]} for relative in expected]))
    tools = root / "tools"
    tools.mkdir()
    return root, tools, sandbox


def static_idf_run(*extra: str) -> tuple[subprocess.CompletedProcess[str], Path,
                                         tempfile.TemporaryDirectory]:
    """Run static-idf end to end over a sandboxed fake compile database."""
    root, tools, sandbox = fake_database_sandbox()
    stub = tools / "cppcheck"
    stub.write_text("#!/bin/sh\nprintf 'cppcheck-stub\\n'\n")
    stub.chmod(0o755)
    env = os.environ.copy()
    env.update(SS_CLI_ROOT=str(root), SHOTSTOPPER_NONINTERACTIVE="1",
               PATH=f"{tools}:{os.environ['PATH']}")
    result = subprocess.run(
        ["bash", str(ROOT / "scripts/static-idf"), "--arch", "n16r8",
         "--build-dir", str(root / "build-idf" / "n16r8"), *extra],
        cwd=ROOT, env=env, capture_output=True, text=True)
    return result, root / "reports" / "static-idf-golden", sandbox


static_idf_result, static_idf_report, static_idf_sandbox = static_idf_run(
    "--output-dir", "reports/static-idf-golden")
try:
    assert static_idf_result.returncode == 0, (
        static_idf_result.returncode, static_idf_result.stdout,
        static_idf_result.stderr)
    assert re.search(r"translation-unit coverage: (\d+)/\1 production C\+\+ "
                     "files for integration=none", static_idf_result.stdout), \
        static_idf_result.stdout
    assert "Reports saved under reports/static-idf-golden/" in static_idf_result.stdout
    assert "cppcheck-stub" in (static_idf_report / "cppcheck.txt").read_text()
    static_idf_readme = (static_idf_report / "README.txt").read_text()
    assert "Cppcheck exit status: 0" in static_idf_readme
    assert "Build database: " in static_idf_readme
finally:
    static_idf_sandbox.cleanup()


def iwyu_idf_run(*extra: str) -> tuple[subprocess.CompletedProcess[str], Path,
                                       tempfile.TemporaryDirectory]:
    """Run iwyu-idf end to end over a sandboxed fake compile database."""
    root, tools, sandbox = fake_database_sandbox()
    stub = tools / "include-what-you-use"
    stub.write_text(
        "#!/bin/sh\n"
        "if [ \"$1\" = \"--version\" ]; then\n"
        "  printf 'include-what-you-use 0.23-stub\\n'\n"
        "  exit 0\n"
        "fi\n"
        "printf 'iwyu-stub suggestion\\n'\n")
    stub.chmod(0o755)
    env = os.environ.copy()
    env.update(SS_CLI_ROOT=str(root), SHOTSTOPPER_NONINTERACTIVE="1",
               PATH=f"{tools}:{os.environ['PATH']}")
    result = subprocess.run(
        ["bash", str(ROOT / "scripts/iwyu-idf"), "--arch", "n16r8",
         "--build-dir", str(root / "build-idf" / "n16r8"), *extra],
        cwd=ROOT, env=env, capture_output=True, text=True)
    return result, root / "reports" / "iwyu-golden", sandbox


iwyu_result, iwyu_report, iwyu_sandbox = iwyu_idf_run(
    "--output-dir", "reports/iwyu-golden")
try:
    assert iwyu_result.returncode == 0, (
        iwyu_result.returncode, iwyu_result.stdout, iwyu_result.stderr)
    # The shared translation-unit audit now also gates the IWYU run.
    assert re.search(r"translation-unit coverage: (\d+)/\1 production C\+\+ "
                     "files for integration=none", iwyu_result.stdout), \
        iwyu_result.stdout
    parsed = re.search(r"Parsed (\d+) of (\d+) translation units", iwyu_result.stdout)
    assert parsed and parsed.group(1) == parsed.group(2), iwyu_result.stdout
    assert "iwyu-stub suggestion" in (iwyu_report / "iwyu.txt").read_text()
    iwyu_readme = (iwyu_report / "README.txt").read_text()
    assert "Tool: include-what-you-use 0.23-stub" in iwyu_readme
    assert "Build database: " in iwyu_readme and "Mappings: " in iwyu_readme
finally:
    iwyu_sandbox.cleanup()


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


def cli_profile(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["bash", "-c",
         f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
         'ss_cli_parse "$@" || exit $?; ss_cli_flags_for development release '
         'no_auth_admin jtag; '
         'printf "%s|%s" "$(ss_cli_effective_flags)" '
         '"${SS_CLI_FORWARD[*]:-}"', "probe", *args],
        cwd=ROOT, capture_output=True, text=True)


release_profile = cli_profile("--release")
assert release_profile.returncode == 0 and release_profile.stdout == "|--release"
no_profile = cli_profile()
assert no_profile.returncode == 0 and no_profile.stdout == "|", no_profile.stdout
release_jtag = cli_profile("--release", "--jtag")
assert release_jtag.returncode == 0 and \
    release_jtag.stdout == "-DSHOT_STOPPER_ENABLE_JTAG=1|--release --jtag"
admin_unlock = cli_profile("--no-auth-admin")
assert admin_unlock.returncode == 0 and \
    admin_unlock.stdout == "-DSHOT_STOPPER_DEVELOPMENT=1|--no-auth-admin"
dev_profile = cli_profile("--development")
assert dev_profile.returncode == 0 and dev_profile.stdout == (
    "-DSHOT_STOPPER_ENABLE_JTAG=1 -DSHOT_STOPPER_DEVELOPMENT=1"
    "|--development --no-auth-admin --jtag")
release_wins = cli_profile("--release", "--no-auth-admin")
assert release_wins.returncode == 0 and release_wins.stdout == (
    "-DSHOT_STOPPER_DEVELOPMENT=1|--release --no-auth-admin")
profile_conflict = cli_profile("--development", "--release")
assert profile_conflict.returncode == 2 and \
    "mutually exclusive" in profile_conflict.stderr
admin_conflict = cli_probe("--no-auth-admin", "--flags=-DSHOT_STOPPER_DEVELOPMENT=0")
assert admin_conflict.returncode == 2 and \
    "--no-auth-admin conflicts with SHOT_STOPPER_DEVELOPMENT=0" in admin_conflict.stderr

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


def cli_opt_level(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["bash", "-c",
         f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
         'ss_cli_parse "$@" || exit $?; ss_cli_flags_for opt_level; '
         'printf "%s" "${SS_CLI_FORWARD[*]:-}"', "probe", *args],
        cwd=ROOT, capture_output=True, text=True)


for level in ("--o0", "--og", "--o2", "--os"):
    forwarded = cli_opt_level(level)
    assert forwarded.returncode == 0 and forwarded.stdout == level, level
opt_none = cli_opt_level()
assert opt_none.returncode == 0 and opt_none.stdout == ""
opt_exclusive = cli_opt_level("--o2", "--os")
assert opt_exclusive.returncode == 2 and "exclusive" in opt_exclusive.stderr
opt_repeat = cli_opt_level("--o2", "--o2")
assert opt_repeat.returncode == 0 and opt_repeat.stdout == "--o2"
for level in ("--o1", "--o3", "--o1=1", "--oz", "--Ofast"):
    rejected = cli_opt_level(level)
    assert rejected.returncode == 2 and "not an ESP-IDF Kconfig" in rejected.stderr, level
opt_not_stored = subprocess.run(
    ["bash", "-c",
     f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
     'ss_cli_parse "$@" || exit $?; ss_cli_save; cat "$SS_CLI_STORE"',
     "probe", "--os", "--flags=-DCUSTOM=1"],
    cwd=ROOT, env={**os.environ, "SS_CLI_ROOT": str(Path(mkdtemp()))},
    capture_output=True, text=True)
assert opt_not_stored.returncode == 0 and \
    "opt_level" not in opt_not_stored.stdout and \
    "--os" not in opt_not_stored.stdout
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
     'ss_cli_parse --development --release || exit $?; ss_cli_effective_flags'],
    cwd=ROOT, capture_output=True, text=True)
assert development_conflict.returncode == 2 and \
    "mutually exclusive" in development_conflict.stderr

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
    assert all("--development" in argv and "--jtag" not in argv
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

build_opt = stubbed_dispatcher(
    ("build",), [*profile_args, "--webui-language", "EN_us", "--o0"])
assert build_opt == [] or "--o0" in build_opt[0]
assert len(build_opt) == 1

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
            (env / "idf_version.txt").write_text(
                "6.1" if version is not None else "\n")
            (idf / "tools/idf.py").write_text(
                f"#!/bin/sh\necho 'ESP-IDF v{version}'\n")
            (idf / "tools/idf.py").chmod(0o755)
            (idf / "components/esp_common/include").mkdir(parents=True,
                                                          exist_ok=True)
            (idf / "components/esp_common/include/esp_idf_version.h").write_text(
                "#define ESP_IDF_VERSION_MAJOR 6\n"
                "#define ESP_IDF_VERSION_MINOR 1\n")
        (active / "export.sh").write_text("return 99\n")
        (fallback / "export.sh").write_text(
            f'export IDF_PATH="{fallback}"\n'
            'if [ -n "${IDF_PYTHON_ENV_PATH:-}" ] && '
            '[ -x "${IDF_PYTHON_ENV_PATH}/bin/python" ]; then\n'
            '  :\n'
            'else\n'
            f'  export IDF_PYTHON_ENV_PATH="{fallback_env}"\n'
            'fi\n'
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


def idf_python_env_selection(python_env: str | None, home_leaf: str = "home"):
    """Run the fallback path with an optional installed python_env layout."""
    with tempfile.TemporaryDirectory(prefix="shotstopper-idf-env-") as temporary:
        root = Path(temporary)
        home = root / home_leaf
        fallback = home / "esp/esp-idf-v6.1"
        missing_env = home / ".espressif/python_env/idf6.1_py3.11_env"
        installed_env = home / ".espressif/python_env/idf6.1_py3.14_env"
        for directory in (fallback / "tools",
                          fallback / "components/esp_common/include",
                          missing_env / "bin", installed_env / "bin"):
            directory.mkdir(parents=True)
        (fallback / "tools/idf.py").write_text("#!/bin/sh\necho 'ESP-IDF v6.1'\n")
        (fallback / "tools/idf.py").chmod(0o755)
        (fallback / "components/esp_common/include/esp_idf_version.h").write_text(
            "#define ESP_IDF_VERSION_MAJOR   6\n"
            "#define ESP_IDF_VERSION_MINOR   1\n")
        if python_env == "installed":
            (installed_env / "bin/python").write_text("#!/bin/sh\nexit 0\n")
            (installed_env / "bin/python").chmod(0o755)
            (installed_env / "idf_version.txt").write_text("6.1")
        (fallback / "export.sh").write_text(
            f'export IDF_PATH="{fallback}"\n'
            'if [ -n "${IDF_PYTHON_ENV_PATH:-}" ] && '
            '[ -x "${IDF_PYTHON_ENV_PATH}/bin/python" ]; then\n'
            '  :\n'
            'else\n'
            f'  export IDF_PYTHON_ENV_PATH="{missing_env}"\n'
            'fi\n'
            f'export PATH="{fallback}/tools:$PATH"\n')
        env = os.environ.copy()
        env.update(HOME=str(home), PATH="/usr/bin:/bin")
        for name in ("IDF_PATH", "IDF_PYTHON_ENV_PATH", "ESP_PYTHON",
                     "ESP_IDF_VERSION", "IDF_DEACTIVATE_FILE_PATH"):
            env.pop(name, None)
        expected = (str(installed_env) if python_env == "installed"
                    else str(missing_env))
        command = (
            f'source "{ROOT / "scripts/shotstopper_idf.sh"}"; '
            'SS_IDF_QUIET=1; ss_idf_source; '
            'printf "%s|%s" "$IDF_PATH" "$IDF_PYTHON_ENV_PATH"')
        result = subprocess.run(["bash", "-c", command], env=env,
                                capture_output=True, text=True)
        return result, fallback, installed_env, missing_env, expected


selection, fallback_root, installed_env, missing_env, expected = \
    idf_python_env_selection("installed")
assert selection.returncode == 0 and selection.stdout == (
    f"{fallback_root}|{expected}"), selection.stderr
selection, fallback_root, _, _, expected = idf_python_env_selection("missing")
assert selection.returncode == 0 and selection.stdout == (
    f"{fallback_root}|{expected}"), selection.stderr
# A HOME containing spaces must not break the environment scan.
selection, fallback_root, _, _, expected = \
    idf_python_env_selection("installed", home_leaf="my esp home")
assert selection.returncode == 0 and selection.stdout == (
    f"{fallback_root}|{expected}"), selection.stderr


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
            "size = '36K' if os.environ['FLASH_LAYOUT'] == 'size' else '56K'\n"
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
        (tools / "esptool").write_text(
            "#!/usr/bin/env python3\nimport os, pathlib, sys\n"
            f"open({str(log)!r}, 'a').write('esptool:' + ' '.join(sys.argv[1:]) + '\\n')\n"
            "if 'read-flash' in sys.argv:\n"
            "    if os.environ['FLASH_LAYOUT'] == 'unknown': sys.exit(1)\n"
            "    fill = 255 if os.environ['FLASH_LAYOUT'] == 'blank' else 0\n"
            "    pathlib.Path(sys.argv[-1]).write_bytes(bytes([fill]) * 4096)\n")
        for executable in (tools / "idf.py", tools / "node", tools / "python",
                           tools / "esptool"):
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
    any("write-flash @flash_args" in line for line in blank_flash), blank_result.stderr
erased_result, erased_flash = flash_command("missing", "--erase-all")
assert erased_result.returncode == 0 and \
    any("erase-flash" in line for line in erased_flash) and \
    any("write-flash @flash_args" in line for line in erased_flash), erased_flash
present_result, present_flash = flash_command("present")
assert present_result.returncode == 0 and \
    any("write-flash @flash_args" in line for line in present_flash) and \
    not any(" flash" in line for line in present_flash if line.startswith("idf:")), present_flash
present_n16_result, _ = flash_command("present", arch="n16r8")
assert present_n16_result.returncode == 0, present_n16_result.stderr
unchecked_result, unchecked_flash = flash_command("present", "--no-check")
assert unchecked_result.returncode == 0 and \
    any("write-flash @flash_args" in line for line in unchecked_flash) and \
    not any(" flash" in line for line in unchecked_flash if line.startswith("idf:")), \
    unchecked_flash
external_result, external_flash = flash_command("present", "--image", "placeholder")
assert external_result.returncode == 0 and \
    any("write-flash 0x20000" in line for line in external_flash) and \
    not any(" flash" in line for line in external_flash if line.startswith("idf:")), \
    external_flash
for incompatible_layout in ("missing", "wrong", "type", "subtype", "size"):
    for extra in ((), ("--image", "placeholder")):
        rejected, commands = flash_command(incompatible_layout, *extra)
        assert rejected.returncode == 1 and "required n8r4 shotcurve" in rejected.stderr, \
            (incompatible_layout, extra, rejected.stderr)
        assert not any("write-flash" in line for line in commands), commands
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
cppcheck_help = subprocess.run([str(ROOT / "scripts/static-idf"), "--help"],
                              cwd=ROOT, text=True, capture_output=True)
assert cppcheck_help.returncode == 0 and "`unusedFunction`" in cppcheck_help.stdout
assert "--arch" in cppcheck_help.stdout and "command not found" not in cppcheck_help.stderr
assert "github.event_name != 'pull_request'" in idf_job, \
    "main, scheduled, and manual CI runs must publish firmware profiles"
resolver = runpy.run_path(str(ROOT / "scripts/resolve_build_profiles.py"))
hardware = [resolver["validate_hardware"](json.loads(path.read_text()))
            for path in (ROOT / "config/hardware").glob("*.json")]
machines = [resolver["validate_machine"](json.loads(path.read_text()))
            for path in (ROOT / "config/machines").glob("*.json")]
supported_pairs = set()
for hardware_profile in hardware:
    for machine_profile in machines:
        if machine_profile["interface"]["feedback"] != "none" or (
                machine_profile["integration"] == "linea_micra_cloud" and
                hardware_profile["reed"]["present"]):
            continue
        try:
            resolver["resolve"](hardware_profile, machine_profile, "")
        except resolver["ProfileError"]:
            continue
        supported_pairs.add((hardware_profile["id"], machine_profile["id"]))
local_pairs = dev_module["BUILD_PROFILES"]
assert len(local_pairs) == len(supported_pairs) and set(local_pairs) == supported_pairs, \
    "local validation must cover every supported profile pair exactly once"
ci_rows = re.findall(
    r"- \{name: ([^,]+), hardware: ([^,]+), machine: ([^}]+)\}", idf_job)
assert len(ci_rows) == len(supported_pairs) and \
    len({name for name, _, _ in ci_rows}) == len(ci_rows) and \
    {(hardware, machine) for _, hardware, machine in ci_rows} == supported_pairs, \
    "CI matrix must name every supported pair exactly once"
for disabled_flag in ("SHOT_STOPPER_ENABLE_JTAG=0",
                      "SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=0"):
    assert disabled_flag in idf_job, f"CI production flag missing: {disabled_flag}"
validation_build = idf_job.split("- name: Build validation firmware", 1)[1].split(
    "- name: Cppcheck", 1)[0]
assert "--os" in validation_build, "validation build must pin the --os optimization level"
assert "--development" in validation_build and "--jtag" not in validation_build, \
    "CI resource validation must use the development build profile"
installable_build = idf_job.split("- name: Build installable firmware", 1)[1].split(
    "- name: Name OTA image", 1)[0]
assert "--os" in installable_build and "--release" in installable_build, \
    "installable build must pin the --os optimization level with --release"
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
installable_build = idf_job.index("./scripts/dev build", build + 1)
assert build < cppcheck < tidy < warnings < gcc_analyzer < installable_build < firmware_upload
assert cppcheck < iwyu < warnings
assert idf_job.count("set -o pipefail") == 7 and idf_job.count("tee ci-results/idf/") == 7, \
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
