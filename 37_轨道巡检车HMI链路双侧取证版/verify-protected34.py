from pathlib import Path
import hashlib, json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
EVIDENCE = HERE / "运行核验" / "34号实施_20261103"
rows = json.loads((EVIDENCE / "31_33保护基线.json").read_text(encoding="utf-8-sig"))
changed = []
count = 0
for row in rows:
    path = ROOT / row["path"].replace("\\\\", "\\")
    if not path.exists():
        changed.append({"path": row["path"], "reason": "missing"})
        continue
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    count += 1
    if len(data) != row["size"] or digest.lower() != row["sha256"].lower():
        changed.append({"path": row["path"], "reason": "changed", "size": len(data),
                        "sha256": digest, "expected": row["sha256"]})
result = {"protectedFiles": count, "expectedFiles": len(rows), "changed": changed,
          "pass": not changed}
(EVIDENCE / "34号保护基线复核.json").write_text(
    json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(result, ensure_ascii=False))
