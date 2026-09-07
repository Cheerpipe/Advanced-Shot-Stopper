#!/usr/bin/env python3
"""Golden contracts for the developer facade."""

import subprocess
from pathlib import Path
import re
import csv

ROOT = Path(__file__).resolve().parents[2]
DEV = ROOT / "scripts/dev"


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

scripts_text = "\n".join(
    path.read_text(errors="replace") for path in (ROOT / "scripts").iterdir()
    if path.is_file())
assert not re.search(r"--(?:token|password)\s+['\"]", scripts_text), \
    "credentials must not be forwarded in argv"
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
build = idf_job.index("./scripts/dev build")
firmware_upload = idf_job.index("actions/upload-artifact")
cppcheck = idf_job.index("./scripts/dev analyze")
tidy = idf_job.index("./scripts/static-tidy-idf")
iwyu = idf_job.index("./scripts/iwyu-idf")
warnings = idf_job.index("./scripts/warnings-idf")
gcc_analyzer = idf_job.index("./scripts/gcc_analyzer")
assert build < firmware_upload < cppcheck < tidy < warnings < gcc_analyzer
assert cppcheck < iwyu < warnings
assert "compile_commands.json" not in idf_job, \
    "compile commands are not a portable standalone artifact"
gate_job = workflow.split("  gate:\n", 1)[1]
assert "needs.analysis" not in gate_job and "ANALYSIS:" not in gate_job, \
    "the gate must use the combined IDF build and analysis result"
for use in re.findall(r"uses:\s*([^\s]+)", workflow):
    assert re.search(r"@[0-9a-f]{40}$", use), f"action is not SHA-pinned: {use}"
assert "cancel-in-progress: true" in workflow and "contents: read" in workflow
assert "scripts/flash" not in workflow and "scripts/ota-idf" not in workflow
template = (ROOT / ".github/PULL_REQUEST_TEMPLATE.md").read_text()
for field in ("Risk", "Safety invariants", "Validation", "HIL/manual evidence"):
    assert field in template, f"PR template field missing: {field}"
print(f"developer facade: {len(golden)} risk cases, 11 context areas, and script contracts passed")
