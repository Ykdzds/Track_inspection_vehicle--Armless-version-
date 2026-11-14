import hashlib
import json
import os
import sys
from pathlib import Path

project = Path(__file__).resolve().parents[1]
baseline = Path(sys.argv[1]) if len(sys.argv) > 1 else Path((Path(os.environ["TEMP"]) / "f103_oled_uart_active.txt").read_text().strip())
before = json.loads((baseline / "before_hashes.json").read_text(encoding="utf-8-sig"))
protected = {name: digest for name, digest in before.items() if not Path(name).is_relative_to(project)}
roots = set()
for name in protected:
    path = Path(name)
    roots.add(next(parent for parent in path.parents if parent.name in ("RailInspectionCar_Inno_V1", "05_Upper_level_CAN_com_v1.1_readonly", "05_Upper_level_CAN_com_test")))
changes = []
for name, expected in protected.items():
    path = Path(name)
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        changes.append(name)
for root in roots:
    for path in root.rglob("*"):
        if path.is_file() and ".git" not in path.parts and str(path) not in before:
            changes.append("NEW " + str(path))
output = project / "Verification/OLEDUART"
report = [f"Baseline: {baseline}", f"Protected files: {len(protected)}", f"Changes: {len(changes)}", *changes]
(output / "protection.log").write_text("\n".join(report) + "\n", encoding="utf-8")
print("\n".join(report[:3]))
if changes:
    raise RuntimeError("Protected projects changed")
files = []
for directory in ("APP", "BSP/OLED", "Core", "Tests"):
    files.extend(path for path in (project / directory).rglob("*") if path.is_file() and "__pycache__" not in path.parts)
files.extend((project / name) for name in ("MDK-ARM/DigitalElectronicseCode.uvprojx", "MDK-ARM/upper_sim.sct", "DigitalElectronicseCode.ioc", "OLED_USART1修复交付说明_20261022.md", "README_上层CAN模拟.md"))
files.extend(path for path in output.rglob("*") if path.is_file() and path.name not in ("delivery_manifest.sha256",))
manifest = [hashlib.sha256(path.read_bytes()).hexdigest().upper() + "  " + path.relative_to(project).as_posix() for path in sorted(set(files))]
(output / "delivery_manifest.sha256").write_text("\n".join(manifest) + "\n", encoding="utf-8")
print(f"Manifest: {len(manifest)} files")
