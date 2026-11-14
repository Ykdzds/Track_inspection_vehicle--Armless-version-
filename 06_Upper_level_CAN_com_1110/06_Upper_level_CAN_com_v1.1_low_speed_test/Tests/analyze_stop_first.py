#!/usr/bin/env python3
"""Read-only CANAGE STOP_FIRST decoder; missing pages remain explicit."""
import argparse
import json
from pathlib import Path
import re

SOURCES = {1: "CTRL_STALE", 2: "AXIS_STALE", 3: "AXIS_UNSAFE", 4: "UI_STOP",
           5: "UI_QUICK_STOP", 6: "UI_RELEASE", 7: "AUTH_TIMEOUT", 8: "ZERO_TIMEOUT"}


def analyze(text):
    groups, warnings = [], []
    current = None
    for line_number, line in enumerate(text.splitlines(), 1):
        match = re.search(r"STOP_FIRST v=1\s+([^\r\n]+)", line)
        if not match:
            continue
        fields = dict(re.findall(r"(\w+)=([^\s]+)", match[1]))
        try:
            part, at = int(fields["part"]), int(fields["at"])
            if part not in (0, 1, 2):
                raise ValueError("invalid page")
            if part == 0 or current is None or current["at_ms"] != at:
                current = {"at_ms": at, "pages": {}, "lines": []}
                groups.append(current)
            if part in current["pages"]:
                warnings.append(f"line {line_number}: repeated page {part}")
            current["pages"][part] = fields
            current["lines"].append(line_number)
        except (KeyError, ValueError) as error:
            warnings.append(f"line {line_number}: {error}")
    for event in groups:
        event["missing_pages"] = [page for page in range(3) if page not in event["pages"]]
        first = event["pages"].get(0, {})
        try:
            event["source"] = SOURCES.get(int(first.get("source", "0")), "UNKNOWN")
        except ValueError:
            event["source"] = "UNKNOWN"
            warnings.append(f"at {event['at_ms']}: invalid source")
        event["boot_pair"] = first.get("boot", "UNKNOWN")
        event["token_pair"] = first.get("token", "UNKNOWN")
    return {"events": groups, "warnings": warnings}


def read_text(path):
    raw = Path(path).read_bytes()
    encodings = ("utf-16",) if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else ("utf-8-sig", "gb18030")
    for encoding in encodings:
        try:
            return raw.decode(encoding)
        except UnicodeError:
            pass
    return raw.decode("utf-8", errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    result = analyze(read_text(args.log))
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return
    for event in result["events"]:
        p = event["pages"].get(1, {})
        print(f"STOP_FIRST at={event['at_ms']} source={event['source']} "
              f"boot={event['boot_pair']} token={event['token_pair']} "
              f"ctrl_age={p.get('ctrl_age', 'UNKNOWN')} "
              f"axis_age={p.get('axis_age', 'UNKNOWN')} "
              f"missing={event['missing_pages']}")
    for warning in result["warnings"]:
        print("WARNING", warning)
    print(f"{len(result['events'])} decisions; local timestamps are not a shared F407 clock")


if __name__ == "__main__":
    main()
