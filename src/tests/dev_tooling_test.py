#!/usr/bin/env python3
"""Golden contracts for the developer facade."""

import subprocess
from pathlib import Path
import os
import re
import csv
import runpy
import shutil
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
DEV = ROOT / "scripts/dev"


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

for area in ("safety", "control", "machine", "scale", "ble", "network",
             "ota", "persistence", "web", "build", "tests"):
    result = run("context", area)
    assert result.returncode == 0 and area in result.stdout, (area, result.stderr)

dry_run = run("clean", "--dry-run")
assert dry_run.returncode == 0 and "build-host" in dry_run.stdout

for script in (ROOT / "scripts").iterdir():
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
                 "ffat": (0x620000, 0x9D0000),
                 "coredump": (0xFF0000, 0x10000)},
    },
    "partitions-n8r4.csv": {
        "flash": 0x800000,
        "rows": {"nvs": (0x9000, 0x15000), "otadata": (0x1E000, 0x2000),
                 "app0": (0x20000, 0x330000), "app1": (0x350000, 0x330000),
                 "spiffs": (0x680000, 0x170000),
                 "coredump": (0x7F0000, 0x10000)},
    },
}
for filename, contract in partition_contracts.items():
    rows = partition_rows(filename)
    assert rows == contract["rows"], (filename, rows)
    ordered = sorted(rows.items(), key=lambda item: item[1][0])
    for (_, (offset, size)), (_, (next_offset, _)) in zip(ordered, ordered[1:]):
        assert offset + size <= next_offset, f"partition overlap in {filename}"
    last_offset, last_size = ordered[-1][1]
    assert last_offset + last_size == contract["flash"]
    data_name = "ffat" if "ffat" in rows else "spiffs"
    assert rows[data_name][1] >= 24 * 1024, "shot-curve sidecar no longer fits"

flash_idf = (ROOT / "scripts/flash-idf").read_text()
for required in ("read_flash 0x8000 0x1000", "installed_nvs_bytes != 0x15000",
                 "installed_layout=blank", "erase_flash",
                 '"$installed_app0_offset" "$image"'):
    assert required in flash_idf, f"flash-idf migration contract missing: {required}"
assert '0x10000 "$image"' not in flash_idf, \
    "external app offset must be dynamic"
for wrapper in ("bf-idf", "bfm-idf", "bsfm-idf"):
    assert "erase_all" in (ROOT / "scripts" / wrapper).read_text(), \
        f"{wrapper} does not forward --erase-all"

parsed_erase = subprocess.run(
    ["bash", "-c",
     f'source "{ROOT / "scripts/shotstopper_cli.sh"}"; '
     'ss_cli_parse --erase-all; test "$SS_CLI_ERASE_ALL" = 1'],
    cwd=ROOT, capture_output=True, text=True)
assert parsed_erase.returncode == 0, parsed_erase.stderr
erase_image = subprocess.run(
    [str(ROOT / "scripts/flash-idf"), "--erase-all", "--image", "external.bin"],
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
    assert len(build_steps) == 4
    assert all(argv.count("--webui-language") == 1 and
               argv[argv.index("--webui-language") + 1] == expected
               for argv in build_steps), build_steps


def stubbed_script(script: str, args: list[str], children: tuple[str, ...]) -> list[str]:
    with tempfile.TemporaryDirectory(prefix="shotstopper-wrapper-") as temporary:
        root = Path(temporary)
        scripts = root / "scripts"
        scripts.mkdir()
        for name in (script, "shotstopper_cli.sh", "shotstopper_board.sh"):
            shutil.copy2(ROOT / "scripts" / name, scripts / name)
        log = root / "children.log"
        for child in children:
            target = scripts / child
            target.write_text(
                f'#!/bin/sh\nprintf "%s:%s\\n" "${{0##*/}}" "$*" >>"{log}"\n')
            target.chmod(0o755)
        env = os.environ.copy()
        env.update(SS_CLI_ROOT=str(root), SHOTSTOPPER_NONINTERACTIVE="1",
                   SHOTSTOPPER_DEVICE_PASSWORD="test-only")
        result = subprocess.run([str(scripts / script), *args], cwd=root, env=env,
                                capture_output=True, text=True)
        assert result.returncode == 0, (script, result.stdout, result.stderr)
        return log.read_text().splitlines()


wrapper_cases = {
    "bf-idf": (["--port", "/dev/null", "--arch", "n8r4"],
               ("build-idf", "flash-idf")),
    "bfm-idf": (["--port", "/dev/null", "--arch", "n8r4", "--speed", "115200"],
                ("build-idf", "flash-idf", "monitor-idf")),
    "bo-idf": (["--arch", "n8r4", "--host", "127.0.0.1"],
               ("build-idf", "o-idf")),
    "bsfm-idf": (["--port", "/dev/null", "--arch", "n8r4", "--speed", "115200"],
                 ("build-idf", "static-idf", "flash-idf", "monitor-idf")),
}
for wrapper, (args, children) in wrapper_cases.items():
    lines = stubbed_script(wrapper, [*args, "--webui-language", "EN_us"], children)
    build_line = next(line for line in lines if line.startswith("build-idf:"))
    assert build_line.count("--webui-language en-us") == 1, (wrapper, lines)
    assert all("--webui-language" not in line for line in lines if line != build_line), lines

for analyzer, child in (("warnings-idf", "build-idf"), ("gcc_analyzer", "build")):
    lines = stubbed_script(
        analyzer, ["--arch", "n8r4", "--webui-language=EN_us"], (child,))
    assert len(lines) == 1 and "--webui-language en-us" in lines[0], lines

for alias, target in (("b", "build"), ("b-idf", "build-idf"),
                      ("build", "build-idf"), ("bf", "bf-idf"),
                      ("bfm", "bfm-idf"), ("bo", "bo-idf"),
                      ("bsfm", "bsfm-idf")):
    assert f'exec "$SCRIPT_DIR/{target}" "$@"' in (ROOT / "scripts" / alias).read_text(), \
        f"{alias} must preserve transparent argument forwarding"


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


def flash_command(layout: str, *extra: str) -> list[str]:
    with tempfile.TemporaryDirectory(prefix="shotstopper-flash-") as temporary:
        root = Path(temporary)
        idf = root / "fake-idf"
        tools = idf / "tools"
        python_env = root / "python-env/bin"
        build = root / "build-idf/n8r4"
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
            "from pathlib import Path\nimport sys\n"
            "Path(sys.argv[-1]).write_text('nvs,data,nvs,0x9000,0x15000\\n'"
            "+'app0,app,ota_0,0x20000,0x330000\\n')\n")
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
                   FLASH_LAYOUT=layout, TMPDIR=str(root),
                   PATH=f"{tools}:/usr/bin:/bin")
        args = [str(ROOT / "scripts/flash-idf"), "--port", "/dev/null",
                "--arch", "n8r4", *extra]
        if "--image" in extra:
            args[args.index("--image") + 1] = str(image)
        result = subprocess.run(args, env=env, cwd=ROOT, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        return log.read_text().splitlines()


blank_flash = flash_command("blank")
assert any("write_flash @flash_args" in line for line in blank_flash), blank_flash
erased_flash = flash_command("unknown", "--erase-all")
assert any("erase_flash" in line for line in erased_flash) and \
    any("write_flash @flash_args" in line for line in erased_flash), erased_flash
present_flash = flash_command("present")
assert any("write_flash @flash_args" in line for line in present_flash) and \
    not any(" flash" in line for line in present_flash if line.startswith("idf:")), present_flash
unchecked_flash = flash_command("present", "--no-check")
assert any("write_flash @flash_args" in line for line in unchecked_flash) and \
    not any(" flash" in line for line in unchecked_flash if line.startswith("idf:")), \
    unchecked_flash
external_flash = flash_command("present", "--image", "placeholder")
assert any("write_flash 0x20000" in line for line in external_flash) and \
    not any(" flash" in line for line in external_flash if line.startswith("idf:")), \
    external_flash
flash_language = subprocess.run(
    [str(ROOT / "scripts/flash-idf"), "--webui-language", "en"],
    cwd=ROOT, capture_output=True, text=True)
assert flash_language.returncode == 2 and "build-only" in flash_language.stderr

scripts_text = "\n".join(
    path.read_text(errors="replace") for path in (ROOT / "scripts").iterdir()
    if path.is_file())
idf_helpers = (ROOT / "scripts/shotstopper_idf.sh").read_text()
assert 'IDF_DEFAULT_HOME="${HOME}/esp/esp-idf-v6.1"' in idf_helpers, \
    "official SDK discovery must use the versioned ESP-IDF 6.1 checkout"
assert "python@3.12" not in idf_helpers, \
    "ESP-IDF 6.1 must activate the Python environment created by its installer"
assert '. "${idf_root}/export.sh" >/dev/null' in idf_helpers, \
    "non-interactive builds must not print ESP-IDF shell-completion warnings"
assert r"component_validation\.cmake" in idf_helpers and \
    'idf "esp_wifi/"' in idf_helpers and 'idf "wpa_supplicant/"' in idf_helpers, \
    "only the known ESP-IDF 6.1 Wi-Fi component warnings may be filtered"
assert 'build 2>&1 | ss_idf_filter_output' in \
    (ROOT / "scripts/build-idf").read_text(), \
    "the firmware build must use the scoped external-warning filter"
for alias, target in (("build", "build-idf"), ("bo", "bo-idf")):
    assert f'exec "$SCRIPT_DIR/{target}" "$@"' in (ROOT / "scripts" / alias).read_text(), \
        f"{alias} must remain an ESP-IDF compatibility alias"
assert not re.search(r"--(?:token|password)\s+['\"]", scripts_text), \
    "credentials must not be forwarded in argv"
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
    "main, scheduled, and manual CI runs must publish both firmware variants"
assert "arch: [n8r4, n16r8]" in idf_job, \
    "IDF CI must build both supported firmware variants"
for machine_name, machine_type in (("paddle-latch", 0), ("momentary", 1),
                                   ("momentary-reed", 2)):
    assert f"{{name: {machine_name}, type: {machine_type}}}" in idf_job, \
        f"IDF CI machine variant missing: {machine_name}"
for disabled_flag in ("SHOT_STOPPER_ENABLE_JTAG=0",
                      "SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=0"):
    assert disabled_flag in idf_job, f"CI production flag missing: {disabled_flag}"
ota_name = "shotstopper-ota-${{ matrix.arch }}-${{ matrix.machine.name }}-jtag-off-remote-off"
assert f"name: {ota_name}" in idf_job
assert f"build-idf/${{{{ matrix.arch }}}}/{ota_name}.bin" in idf_job
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
