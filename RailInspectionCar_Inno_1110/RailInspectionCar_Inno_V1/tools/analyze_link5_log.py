#!/usr/bin/env python3
"""Read-only HLD/LD evidence decoder. Uses only the Python standard library."""

import argparse
import json
from pathlib import Path
import re
import sys

HEADER = re.compile(r"(?<![A-Za-z0-9_])(HLD|LD)[ \t]+([0-9A-Fa-f]+)[ \t]+"
                    r"([0-9A-Fa-f]+)[ \t]+([0-9A-Fa-f]+)[ \t]+([A-Za-z][A-Za-z0-9_]*)\b")
FIELD = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=(.*?)(?=\s+[A-Za-z_][A-Za-z0-9_]*=|$)")
STATUSES = ["OK", "INVALID_ARGUMENT", "NOT_INITIALIZED", "QUEUE_FULL", "TIMEOUT",
            "CRC_ERROR", "ADDRESS_ERROR", "FUNCTION_ERROR", "EXCEPTION", "FRAME_ERROR",
            "TRANSPORT_ERROR"]


def unsigned_elapsed(end, start):
    """One 32-bit wrap is supported; half-range ambiguity is not guessed."""
    elapsed = (end - start) & 0xFFFFFFFF
    return elapsed if elapsed < 0x80000000 else None


def signed16(value):
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def raw_prefix(words, length):
    raw = b"".join((word & 0xFFFFFFFF).to_bytes(4, "little") for word in words)
    return raw[:min(length, len(raw), 8)]


def parse_records(text):
    records, warnings = [], []
    ignored_lines = 0
    for line_no, line in enumerate(text.splitlines(), 1):
        matches = list(HEADER.finditer(line))
        if not matches:
            if line.strip():
                ignored_lines += 1
            continue
        for index, match in enumerate(matches):
            body = line[match.end():matches[index + 1].start() if index + 1 < len(matches) else len(line)].strip()
            prefix, at, boot, serial, record_type = match.groups()
            record = {"line": line_no, "record_in_line": index, "prefix": prefix,
                      "at_ms": int(at, 16), "boot": int(boot, 16),
                      "serial": int(serial, 16), "type": record_type, "body": body}
            fields = {key: value.strip() for key, value in FIELD.findall(body)}
            record["fields"] = fields.copy()

            def number(key, base=16):
                if key not in fields:
                    return
                try:
                    record["fields"][key] = int(fields[key], base)
                except ValueError:
                    warnings.append({"line": line_no, "message": "Invalid numeric field " + key})

            def numbers(key, base=16, delimiter=",", allow_na=False):
                if key not in fields:
                    return
                try:
                    record["fields"][key] = [None if allow_na and item == "NA" else int(item, base)
                                              for item in fields[key].split(delimiter)]
                except ValueError:
                    warnings.append({"line": line_no, "message": "Invalid numeric list " + key})

            if prefix == "LD":
                for key in ("kind", "p", "part", "parts", "count", "sample", "id", "merged"):
                    number(key)
                numbers("w")
                record["source"] = fields.get("src", "UNKNOWN")
                if "kind" in record["fields"]:
                    record["kind"] = record["fields"]["kind"]
            elif re.fullmatch(r"E[0-9A-Fa-f]+", record_type):
                record["kind"] = int(record_type[1:], 16)
                try:
                    record["words"] = [int(item, 16) for item in body.split(",")]
                except ValueError:
                    warnings.append({"line": line_no, "message": "Invalid HLD event payload"})
            elif record_type == "FIRST":
                # The legacy format deliberately uses %lu/%u for these values.
                for key in ("at", "owner", "requester", "reason", "seq", "cmd", "decel", "gen"):
                    number(key, 10)
            elif record_type == "BOOT":
                for key in ("diag", "lease", "ttl", "diag_rev"):
                    number(key, 10)
                number("reset")
                numbers("mask", delimiter="/")
            elif record_type == "PREV":
                for key in ("type", "valid", "boot", "at", "detail", "task"):
                    number(key, 10)
                for key in ("code", "pc", "lr", "cfsr", "hfsr"):
                    number(key)
            elif record_type in ("MEM", "LOGGER"):
                for key in fields:
                    number(key, 16 if key in ("reset", "diag") else 10)
            elif record_type == "STACK":
                numbers("bytes", 10, allow_na=True)
            elif record_type == "GAPS":
                numbers("us", 10)
            elif record_type == "SCREEN":
                for key in ("page", "mask", "incomplete"):
                    number(key, 10)
                numbers("words")
            elif record_type in ("SLOW", "SNAP_REJECT", "SNAP"):
                for key, value in fields.items():
                    if "/" in value:
                        numbers(key, delimiter="/")
                    else:
                        number(key, 10 if record_type == "SNAP_REJECT" and key in ("why", "ex") else 16)
            elif record_type == "TIMING":
                try:
                    record["words"] = [int(item, 16) for item in body.split()[0].split(",")]
                except (ValueError, IndexError):
                    warnings.append({"line": line_no, "message": "Invalid TIMING counters"})
                numbers("tc", delimiter="/")
                numbers("slow", delimiter="/")
            elif record_type in ("S", "UART", "RXERR", "DMA", "QUEUE", "QUIET"):
                try:
                    record["words"] = [int(item, 16) for item in body.split(",")]
                except ValueError:
                    warnings.append({"line": line_no, "message": "Invalid " + record_type + " counters"})
            records.append(record)
    return records, warnings, ignored_lines


def transaction_decode(words):
    """The exact 16-word S485 FIRST 12/13 / periodic phase-2 layout."""
    if len(words) < 16:
        return None
    valid = words[12] & 255
    status = (words[12] >> 16) & 255
    out = {"transaction": words[0], "epoch": words[1], "address": words[9] & 0xFFFF,
           "tag": words[9] >> 16, "quantity": words[10] >> 16,
           "written_value": words[10] & 0xFFFF, "motion_owner": words[11] >> 24,
           "protocol_owner": (words[11] >> 16) & 255, "axis": (words[11] >> 8) & 255,
           "slave": words[11] & 255, "function": words[12] >> 24,
           "status": status, "status_name": STATUSES[status] if status < len(STATUSES) else "UNKNOWN",
           "attempt": (words[12] >> 8) & 255, "validity": valid,
           "raw_length": words[13], "raw_prefix_hex": raw_prefix(words[14:16], words[13]).hex(),
           "times_ms": dict(zip(("enqueue", "dequeue", "tx_start", "uart_tc", "rx_capture",
                                  "validation", "consumer"), words[2:9])), "durations_ms": {}}
    times, durations = out["times_ms"], out["durations_ms"]
    for name, start, end, mask in (
            ("queue_wait", "enqueue", "dequeue", 1),
            ("dequeue_to_tx", "dequeue", "tx_start", 2),
            ("tx_to_uart_tc", "tx_start", "uart_tc", 6),
            ("uart_tc_to_rx_capture", "uart_tc", "rx_capture", 12),
            ("rx_capture_to_validation", "rx_capture", "validation", 24),
            ("validation_to_consumer", "validation", "consumer", 48)):
        durations[name] = unsigned_elapsed(times[end], times[start]) if valid & mask == mask else None
    durations["enqueue_to_validation"] = unsigned_elapsed(times["validation"], times["enqueue"]) if valid & 17 == 17 else None
    durations["dequeue_to_validation"] = unsigned_elapsed(times["validation"], times["dequeue"]) if valid & 16 else None
    return out


def zero_decode(words):
    if len(words) < 16:
        return None
    valid, raw_length, address = words[13] >> 24, (words[13] >> 16) & 255, words[13] & 0xFFFF
    raw = raw_prefix(words[14:16], raw_length)
    crc_ok = len(raw) == 7 and crc16(raw[:-2]) == int.from_bytes(raw[-2:], "little")
    structure_ok = len(raw) == 7 and 1 <= raw[0] <= 247 and raw[1:5] == bytes((3, 2, 0, 0))
    # 0x18 establishes RX capture + completed validation. A timestamp of zero may be genuine.
    confirmed = address == 0x0B00 and raw_length == 7 and structure_ok and crc_ok and valid & 24 == 24
    return {"transaction": words[0], "observed_ms": words[1], "epoch": words[2],
            "previous_rpm": signed16(words[3] >> 16), "acknowledged_command_rpm": signed16(words[3]),
            "command_valid": bool(words[4] >> 24), "axis": (words[4] >> 16) & 255,
            "state": words[4] & 0xFFFF, "motion_owner": words[5] >> 24,
            "protocol_owner": (words[5] >> 16) & 255, "tag": words[5] & 0xFFFF,
            "address": address, "validity": valid, "raw_length": raw_length,
            "raw_prefix_hex": raw.hex(), "raw_crc_ok": crc_ok,
            "validated_raw_zero_speed": bool(confirmed), "firmware_zero_hook": True,
            "interpretation": "valid raw zero feedback; physical cause undetermined" if confirmed else
                              "firmware zero hook observed; full validated wire evidence unavailable",
            "times_ms": dict(zip(("enqueue", "dequeue", "tx_start", "uart_tc", "rx_capture",
                                   "validation", "consumer"), words[6:13]))}


def health_flags_decode(flags):
    return {"raw": flags, "fault_code": flags & 0xFFFF,
            "enable_feedback": bool(flags & (1 << 16)), "software_enabled": bool(flags & (1 << 17)),
            "speed_valid": bool(flags & (1 << 18)), "fault_valid": bool(flags & (1 << 19)),
            "enable_valid": bool(flags & (1 << 20)), "online": bool(flags & (1 << 21)),
            "stop_latched": bool(flags & (1 << 22)), "degraded": bool(flags & (1 << 23)),
            "health": (flags >> 24) & 3, "controlled_stop": bool(flags & (1 << 26)),
            "offline": bool(flags & (1 << 27)), "baseline_valid": bool(flags & (1 << 31))}


def safety_flags_decode(flags):
    return {"raw": flags, "phase": flags & 255, "failed_task": (flags >> 8) & 255,
            "boot_locked": bool(flags & (1 << 16)), "baseline_valid": bool(flags & (1 << 31))}


def rs485_transition_decode(kind, words):
    if len(words) != 6:
        return None
    if kind == 0x22:
        item = (words[0] >> 16) & 255
        before, after = health_flags_decode(words[1]), health_flags_decode(words[2])
        valid_key = {0: "speed_valid", 1: "fault_valid", 2: "enable_valid"}.get(item)
        sample_valid = bool(after["baseline_valid"] and valid_key and after[valid_key])
        return {"layout": "rs485_health_transition", "motion_owner": words[0] >> 24,
                "item": item, "item_name": {0: "speed", 1: "fault", 2: "enable"}.get(item, "UNKNOWN"),
                "axis": words[0] & 0xFFFF, "before": before, "after": after,
                "baseline_observation": not before["baseline_valid"],
                "feedback_stop_pending": (words[3] >> 24) & 255,
                "enable_failures": (words[3] >> 16) & 255, "fault_failures": (words[3] >> 8) & 255,
                "speed_failures": words[3] & 255, "feedback_transaction": words[4],
                "feedback_sample_valid": sample_valid, "stored_tick_age_ms": words[5],
                "valid_feedback_age_ms": words[5] if sample_valid else None,
                "physical_root_cause": None}
    if kind == 0x23:
        before, after = safety_flags_decode(words[0]), safety_flags_decode(words[1])
        return {"layout": "rs485_safety_transition", "before": before, "after": after,
                "baseline_observation": not before["baseline_valid"], "full_failure": words[2],
                "generation": words[3], "pending_transaction": words[4],
                "parameters_mask": words[5] >> 24, "zero_mask": (words[5] >> 16) & 255,
                "pending_axis": (words[5] >> 8) & 255, "pending_step": words[5] & 255,
                "physical_root_cause": None}
    return None


def route_decode(source, kind, words):
    if source == "S485" and kind in (0x22, 0x23):
        return rs485_transition_decode(kind, words)
    if source == "S485" and kind == 0x14 and len(words) == 6:
        status, valid = words[2] >> 16, bool(words[3] & 255)
        return {"layout": "rs485_submit_rejection", "transaction": words[0] if valid else None,
                "epoch": words[1], "status": status,
                "status_name": STATUSES[status] if status < len(STATUSES) else "UNKNOWN",
                "metadata_valid": valid, "motion_owner": None,
                "protocol_owner": (words[3] >> 16) & 255 if valid else None,
                "axis": (words[3] >> 8) & 255 if valid else None,
                "tag": words[2] & 0xFFFF if valid else None,
                "slave": words[4] >> 24 if valid else None,
                "function": (words[4] >> 16) & 255 if valid else None,
                "address": words[4] & 0xFFFF if valid else None,
                "quantity": words[5] >> 16 if valid else None,
                "requested_value": words[5] & 0xFFFF if valid else None,
                "accepted": False, "enqueue_ms": None, "raw_wire_reply_available": False}
    if len(words) != 16:
        return None
    if source in ("CS", "CA") and kind == 0x111:
        reason = words[1] & 255
        return {"layout": "can-record-failure-v1", "observed_ms": words[0],
                "failure_reason": reason, "failure_name": {
                    1: "retain_action_failed", 2: "payload_prepare_rejected", 3: "payload_deadline_before_send",
                    4: "start_budget_before_send", 5: "record_or_payload_deadline",
                    6: "payload_deadline_at_tx_result", 7: "tx_retries_exhausted",
                    8: "start_budget_at_tx_result"}.get(reason, "UNKNOWN"),
                "record_type": (words[1] >> 8) & 255, "frame_index": words[1] >> 16,
                "record_sequence": words[2] & 0xFFFF, "record_length": words[2] >> 16,
                "cookie": words[3], "started_ms": words[4], "built_ms": words[5],
                "start_tx_ok_ms": words[6], "last_tx_ok_ms": words[7],
                "tx_result_processed_ms": words[8], "failure_processed_ms": words[9],
                "link_generation": words[10], "link_token": words[11], "state_flags": words[12],
                "payload_deadline_ms": words[13], "max_service_gap_ms": words[14],
                "max_tx_result_delay_ms": words[15],
                "elapsed_at_failure_ms": unsigned_elapsed(words[0], words[4]) if reason in (5, 6, 7, 8) else None}
    if source == "S485":
        if kind == 0x10:
            return {"layout": "rs485_zero", **zero_decode(words)}
        if kind in (0x12, 0x13):
            return {"layout": "rs485_transaction", **transaction_decode(words)}
    if source == "RF" and kind == 0x40:
        return {"layout": "remote_first_stop", "reason": words[0], "deceleration_mm_s2": words[1],
                "state": words[2] & 255, "authorized_at_capture": bool(words[2] & 256),
                "snapshot_present": bool(words[2] & 1024), "motion_owner": (words[2] >> 16) & 255,
                "target_rpm": words[3] - (1 << 32) if words[3] & (1 << 31) else words[3],
                "commanded_rpm": [signed16(words[4]), signed16(words[4] >> 16)],
                "authorization_generation": words[5], "valid_frames": words[6],
                "rf_age_ms": words[8], "batch": words[9], "speed_age_ms": words[12],
                "fault_age_ms": words[13], "last_poll_address": words[14] >> 16,
                "last_poll_result": words[14] & 0xFFFF, "fault_code": words[15] >> 16}
    if source == "CS" and kind in (0x105, 0x106):
        return {"layout": "can_servo_stop", "observed_ms": words[0], "reason_detail": words[1],
                "generation": words[2], "stop_generation": words[3], "state": words[4] & 255,
                "authorized_at_capture": bool(words[4] & 256), "speed_sequence": words[5],
                "speed_age_ms": words[6], "remaining_ms": words[7], "stop_reason": words[14],
                "stop_result": words[15]}
    if source == "CA" and kind in (0x201, 0x202, 0x205, 0x206, 0x207):
        return {"layout": "can_arm_first", "observed_ms": words[0], "reason_detail": words[1],
                "operation": words[2], "input_sequence": words[3], "generation": words[4],
                "owner": words[5] & 255, "state": (words[5] >> 8) & 255,
                "permit": (words[5] >> 16) & 255, "work_lock": (words[5] >> 24) & 255,
                "owner_age_ms": words[9], "input_delay_ms": words[10], "readiness_flags": words[13]}
    return None


def assemble_route_first(records):
    groups = {}
    for record in records:
        if record["prefix"] != "LD" or record["type"] != "FIRST":
            continue
        fields = record["fields"]
        if not isinstance(fields.get("kind"), int) or not isinstance(fields.get("p"), int) or not isinstance(fields.get("w"), list) or not all(isinstance(word, int) for word in fields["w"]):
            continue
        key = (record["boot"], record["source"], record["serial"], fields["kind"])
        group = groups.setdefault(key, {"boot": key[0], "source": key[1], "serial": key[2], "at_ms": record["at_ms"],
                                       "kind": key[3], "pages": {}, "conflicts": [], "merged_max": 0})
        page = str(fields["p"])
        if page in group["pages"] and group["pages"][page]["words"] != fields["w"]:
            group["conflicts"].append(record["line"])
        else:
            group["pages"][page] = {"line": record["line"], "words": fields["w"]}
        if isinstance(fields.get("merged"), int):
            group["merged_max"] = max(group["merged_max"], fields["merged"])
    for group in groups.values():
        # Most link5-r1 producers freeze 16 words; refusal/state transitions freeze six.
        # Unknown kinds retain opaque pages without inferring a record length.
        source, kind = group["source"], group["kind"]
        known = ((source == "RF" and kind == 0x40) or
                 (source == "S485" and kind in (0x10, 0x12, 0x13)) or
                 (source == "CS" and kind in (0x100, 0x101, 0x102, 0x105, 0x106, 0x109, 0x110, 0x111)) or
                 (source == "CA" and kind in (0x110, 0x111, 0x201, 0x202, 0x205, 0x206, 0x207)))
        expected = 6 if source == "S485" and kind in (0x14, 0x22, 0x23) else 16 if known else None
        group["expected_words"] = expected
        expected_pages = list(range((expected + 7) // 8)) if expected else []
        group["complete"] = bool(expected and not group["conflicts"] and
                                  set(group["pages"]) == {str(page) for page in expected_pages} and
                                  all(len(group["pages"][str(page)]["words"]) == min(8, expected - page * 8)
                                      for page in expected_pages))
        group["missing_pages"] = [page for page in expected_pages if str(page) not in group["pages"]] if expected else None
        group["words"] = [word for page in expected_pages for word in group["pages"][str(page)]["words"]] if group["complete"] else None
        group["decoded"] = route_decode(source, kind, group["words"]) if group["complete"] else None
    return list(groups.values())


def assemble_f103_stop_first(text):
    """F103 uses decimal scalar values, explicitly hex masks/boot IDs, no HLD header.
    Pages are immutable at one local decision time; never compare this MCU clock
    directly with F407 time. A missing page cannot identify a complete cause.
    """
    groups, active = [], None
    boot_epoch, banner_upper_boot = 0, None
    required = ({"v", "at", "part", "source", "recovery", "detail", "state", "event_at", "key", "boot", "token", "target"},
                {"v", "at", "part", "valid", "ctrl_age", "axis_age", "hb_age", "ctrl_seq", "speed_seq", "gen", "flags", "owner", "life", "axes"},
                {"v", "at", "part", "axis_flags", "mode", "fault", "merged"})
    hex_fields = {"valid", "boot", "upper_boot", "flags", "axis_flags"}
    for line_no, line in enumerate(text.splitlines(), 1):
        banner = re.search(r"\bUPPER CAN SIM\b.*?\bboot=([0-9A-Fa-f]+)", line)
        if banner:
            # A reboot restarts local milliseconds. An incomplete scene from
            # the previous run must never acquire pages from the next run.
            boot_epoch += 1
            banner_upper_boot = int(banner.group(1), 16)
            active = None
        found = re.search(r"\bSTOP_FIRST\s+(.*)", line)
        if not found:
            continue
        raw = dict(FIELD.findall(found.group(1)))
        try:
            fields = {}
            for key, value in raw.items():
                parts = value.strip().split("/")
                if key == "key":
                    fields[key] = [int(parts[0]), int(parts[1], 16)]
                else:
                    values = [int(part, 16 if key in hex_fields else 10) for part in parts]
                    fields[key] = values if len(values) > 1 else values[0]
            page, at = fields["part"], fields["at"]
            if fields["v"] != 1 or page not in (0, 1, 2) or not required[page] <= fields.keys():
                raise ValueError("invalid page layout")
            pair_fields = ({"key", "boot", "token", "target"}, {"axis_age"}, {"axis_flags", "mode", "fault"})[page]
            if any(not isinstance(fields[key], list) or len(fields[key]) != 2 for key in pair_fields):
                raise ValueError("invalid pair layout")
            if any(isinstance(fields[key], list) for key in required[page] - pair_fields):
                raise ValueError("invalid scalar layout")
            if page == 0 and "upper_boot" in fields and fields["upper_boot"] != fields["boot"][1]:
                raise ValueError("inconsistent producer boot")
        except (ValueError, KeyError, IndexError):
            groups.append({"at_ms": None, "complete": False, "malformed_line": line_no, "decoded": None})
            continue
        producer_boot = fields.get("upper_boot", fields["boot"][1] if page == 0 else banner_upper_boot)
        if producer_boot is None and active is not None:
            producer_boot = active["upper_boot"]
        if (active is None or active["at_ms"] != at or active["upper_boot"] != producer_boot or
                (page == 0 and active["pages"].get("0", {}).get("fields") != fields)):
            active = {"at_ms": at, "upper_boot": producer_boot, "boot_epoch": boot_epoch,
                      "pages": {}, "conflicts": [], "decoded": None}
            groups.append(active)
        old = active["pages"].get(str(page))
        if old is not None and old["fields"] != fields:
            # merged is allowed to advance on final-page UART retry; the scene
            # itself must remain byte-for-byte stable.
            if {k: v for k, v in old["fields"].items() if k != "merged"} != {k: v for k, v in fields.items() if k != "merged"}:
                active["conflicts"].append(line_no)
        active["pages"][str(page)] = {"line": line_no, "fields": fields}
    sources = {1: "AUTO_CONTROL_STALE", 2: "AUTO_AXIS_STALE", 3: "AUTO_AXIS_UNSAFE",
               4: "UI_NORMAL_STOP", 5: "UI_QUICK_STOP", 6: "UI_RELEASE",
               7: "AUTH_TIMEOUT", 8: "FIRST_ZERO_TIMEOUT"}
    for group in groups:
        if "pages" not in group:
            continue
        group["missing_pages"] = [part for part in range(3) if str(part) not in group["pages"]]
        group["complete"] = not group["missing_pages"] and not group["conflicts"]
        if group["complete"]:
            combined = {k: value for part in range(3) for k, value in group["pages"][str(part)]["fields"].items() if k != "part"}
            group["decoded"] = {**combined, "source_name": sources.get(combined["source"], "UNKNOWN"),
                                "clock_domain": "F103 local milliseconds"}
    return groups


def assemble_startup_first(records):
    """Assemble only immutable pages sharing the boot, serial and capture time."""
    groups = {}
    for record in records:
        if record["prefix"] != "LD" or record["type"] != "STARTUP":
            continue
        fields = record["fields"]
        key = (record["boot"], record["serial"], record["at_ms"])
        group = groups.setdefault(key, {"boot": key[0], "serial": key[1], "at_ms": key[2],
                                       "pages": {}, "conflicts": [], "malformed_lines": [],
                                       "count": None, "parts": None, "merged_max": 0})
        part, parts, count, words = (fields.get(name) for name in ("part", "parts", "count", "w"))
        if (not all(isinstance(value, int) for value in (part, parts, count)) or
                not 1 <= count <= 24 or parts != (count + 7) // 8 or not 0 <= part < parts or
                not isinstance(words, list) or len(words) != min(8, count - part * 8) or
                not all(isinstance(word, int) and 0 <= word <= 0xFFFFFFFF for word in words)):
            group["malformed_lines"].append(record["line"])
            continue
        if group["count"] is None:
            group["count"], group["parts"] = count, parts
        if group["count"] != count or group["parts"] != parts:
            group["conflicts"].append(record["line"])
            continue
        page = str(part)
        if page in group["pages"] and group["pages"][page]["words"] != words:
            group["conflicts"].append(record["line"])
        else:
            group["pages"].setdefault(page, {"line": record["line"], "words": words})
        if isinstance(fields.get("merged"), int):
            group["merged_max"] = max(group["merged_max"], fields["merged"])
    for group in groups.values():
        expected = list(range(group["parts"] or 0))
        group["missing_pages"] = [page for page in expected if str(page) not in group["pages"]]
        group["complete"] = bool(group["count"] and not group["missing_pages"] and
                                  not group["conflicts"] and not group["malformed_lines"])
        group["words"] = [word for page in expected for word in group["pages"][str(page)]["words"]] if group["complete"] else None
        group["decoded"] = startup_decode(group["words"]) if group["complete"] else None
    return list(groups.values())


def startup_decode(words):
    """Versioned production layout; unknown or partial captures stay opaque."""
    if len(words) != 24 or words[0] != 0x53540118:
        return None
    source = words[1] >> 24
    axis = (words[1] >> 16) & 255
    result = {"layout": "startup-first-v1", "source": source,
              "source_name": {0: "UNKNOWN", 1: "STARTUP", 2: "BOARD_KEY", 3: "REMOTE",
                              4: "HMI", 5: "CAN", 6: "HEALTH", 7: "DRIVER"}.get(source, "UNKNOWN"),
              "axis": axis if axis < 2 else None, "reason": words[1] & 0xFFFF,
              "detail": words[2], "captured_uptime_ms": words[3],
              "transaction": words[8], "pending_cancelled": words[9] >> 24,
              "pending_axis": (words[9] >> 16) & 255, "pending_tag": words[9] & 0xFFFF,
              "ready_flags": words[22], "prior_failure_codes": words[23], "axes": []}
    for a in range(2):
        phase, action, parameter, flags = words[4+a], words[6+a], words[10+a], words[20+a]
        ages = words[14+a*3:17+a*3]
        result["axes"].append({"axis": a, "startup_stop_state": phase >> 24,
                               "boot_zero_state": (phase >> 16) & 255,
                               "hold_state": (phase >> 8) & 255, "cold_clear_state": phase & 255,
                               "action": action >> 24, "step_index": (action >> 16) & 255,
                               "step_count": (action >> 8) & 255, "reading_back": action & 255,
                               "register": parameter >> 16, "expected": parameter & 0xFFFF,
                               "actual_speed_rpm": signed16(words[12+a] >> 16),
                               "fault_code": words[12+a] & 0xFFFF,
                               "ages_ms": dict(zip(("speed", "fault", "enable"),
                                                    [None if age == 0xFFFFFFFF else age for age in ages])),
                               "flags": flags})
    if source == 2:
        result["key"] = {"event_type": words[1] & 0xFFFF, "index": words[2] >> 16,
                         "sampled_level": words[2] & 0xFFFF}
    return result


def can_stop_identities(records):
    """0x10C relates one wire request to a global event, without guessing payload."""
    result = []
    for record in records:
        words = record.get("fields", {}).get("w", [])
        if (record.get("source") != "CS" or record.get("type") != "EVENT" or record.get("kind") != 0x10C or
                not isinstance(words, list) or len(words) != 6 or not all(isinstance(w, int) for w in words)):
            continue
        received, processed, event, key, requester, repeated = words
        can_id, sequence = key >> 16, key & 0xFFFF
        candidates = []
        for wire in records:
            raw = wire.get("fields", {}).get("w", [])
            expected = (0x100,) if can_id == 0x110 else (0x102, 0x10B) if can_id == 0x080 else ()
            if (wire.get("boot") != record["boot"] or wire.get("source") != "CS" or
                    wire.get("type") != "EVENT" or wire.get("kind") not in expected or not isinstance(raw, list) or
                    len(raw) != 6 or not all(isinstance(w, int) for w in raw) or raw[0] != received):
                continue
            raw_sequence = raw[2] & (0xFF if can_id == 0x110 else 0xFFFF)
            if raw_sequence == sequence:
                candidates.append(wire)
        payloads = {(item["fields"]["w"][2], item["fields"]["w"][3]) for item in candidates}
        payload = next(iter(payloads)) if len(payloads) == 1 else None
        result.append({"layout": "can-stop-wire-identity-v1", "boot": record["boot"], "line": record["line"],
                       "received_ms": received, "processed_ms": processed, "stop_event": event,
                       "can_id": can_id, "request_sequence": sequence, "requester": requester,
                       "repeated_services": repeated, "raw_lines": [item["line"] for item in candidates],
                       "payload_available": payload is not None,
                       "raw_command": (payload[0] >> 8) & 255 if payload and can_id == 0x110 else None,
                       "raw_parameter": payload[0] >> 16 if payload and can_id == 0x110 else None,
                       "raw_words": list(payload) if payload else None})
    return result


def analyze_text(text, dwt_hz=None):
    records, warnings, ignored = parse_records(text)
    boots = {}
    first_stops, observations, periodic_groups = [], [], {}
    timing, measured, slow_groups = [], [], {}
    for record in records:
        boot, fields = record["boot"], record["fields"]
        summary = boots.setdefault(boot, {"boot": boot, "boot_hex": format(boot, "X"), "records": 0,
                                         "types": {}, "heap_samples": [], "stack_min_bytes": [],
                                         "boot_records": [], "logger_records": []})
        summary["records"] += 1
        counter_key = record.get("source", "HMI") + ":" + record["type"]
        summary["types"][counter_key] = summary["types"].get(counter_key, 0) + 1
        if record["prefix"] == "HLD":
            if record["type"] == "FIRST":
                first_stops.append({"boot": boot, "event": record["serial"], "line": record["line"], **fields})
            elif record["type"] == "BOOT":
                summary["boot_records"].append({"line": record["line"], **fields})
            elif record["type"] == "MEM":
                summary["heap_samples"].append({"line": record["line"], **fields})
            elif record["type"] == "STACK" and isinstance(fields.get("bytes"), list):
                for index, value in enumerate(fields["bytes"]):
                    if len(summary["stack_min_bytes"]) <= index:
                        summary["stack_min_bytes"].append(value)
                    elif value is not None:
                        old = summary["stack_min_bytes"][index]
                        summary["stack_min_bytes"][index] = value if old is None else min(value, old)
            elif record["type"] == "LOGGER":
                summary["logger_records"].append({"line": record["line"], **fields})
            elif record["type"] == "TIMING":
                words, tc = record.get("words", []), fields.get("tc", [])
                hz = words[4] if len(words) >= 5 and words[4] else dwt_hz
                entry = {"boot": boot, "line": record["line"], "hz": hz,
                         "last_cycles": tc[0] if isinstance(tc, list) and len(tc) == 2 else None,
                         "max_cycles": tc[1] if isinstance(tc, list) and len(tc) == 2 else None,
                         "slow_counts": fields.get("slow")}
                entry["max_ms"] = entry["max_cycles"] * 1000 / hz if hz and entry["max_cycles"] else None
                timing.append(entry)
                if entry["max_ms"] is not None:
                    measured.append({"boot": boot, "line": record["line"], "duration_ms": entry["max_ms"],
                                     "evidence": "TIMING cumulative capture-to-actual-UART-TC maximum"})
            elif record["type"] == "SLOW" and isinstance(fields.get("p"), int):
                slow_groups.setdefault((boot, record["serial"]), {})[str(fields["p"])] = record
        if record["prefix"] == "LD" and record["type"] in ("STATE", "HEALTH"):
            # No word concatenation: every part is re-projected at its own sample timestamp.
            observation = {"boot": boot, "line": record["line"], "source": record["source"],
                           "type": record["type"], **fields, "atomic_group": False}
            observations.append(observation)
            if isinstance(fields.get("id"), int):
                key = (boot, record["source"], fields["id"])
                group = periodic_groups.setdefault(key, {"boot": boot, "source": record["source"],
                                                         "id": fields["id"], "atomic": False, "observations": []})
                group["observations"].append({key: observation.get(key) for key in ("line", "type", "p", "part", "sample")})

    slow = []
    for (boot, serial), pages in slow_groups.items():
        p0, p1 = pages.get("0", {}), pages.get("1", {})
        a, b = p0.get("fields", {}), p1.get("fields", {})
        hz = b.get("hz") or dwt_hz
        cycles = unsigned_elapsed(a["tc"], a["cap"]) if isinstance(a.get("tc"), int) and isinstance(a.get("cap"), int) else b.get("total")
        entry = {"boot": boot, "transaction": serial, "pages": sorted(pages), "hz": hz,
                 "complete": set(pages) == {"0", "1"}, "duration_cycles": cycles,
                 "duration_ms": cycles * 1000 / hz if hz and cycles is not None else None,
                 "request": a.get("req"), "segments_ms": {}}
        if hz:
            for name, start, end in (("capture_to_boundary", "cap", "boundary"),
                                     ("boundary_to_service", "boundary", "service"),
                                     ("service_to_parse", "service", "parse"),
                                     ("parse_to_submit", "parse", "submit"),
                                     ("submit_to_uart_tc", "submit", "tc")):
                delta = unsigned_elapsed(a[end], a[start]) if isinstance(a.get(start), int) and isinstance(a.get(end), int) else None
                entry["segments_ms"][name] = delta * 1000 / hz if delta is not None else None
        slow.append(entry)
        if entry["duration_ms"] is not None:
            measured.append({"boot": boot, "transaction": serial, "line": p0.get("line", p1.get("line")),
                             "duration_ms": entry["duration_ms"], "evidence": "SLOW explicit capture/actual-TC transaction"})

    routes = assemble_route_first(records)
    transactions, zeroes, wire_events, validation_events, consumer_events = [], [], [], [], []
    submit_rejections, speed_requests, speed_submissions = [], [], []
    health_transitions, safety_transitions = [], []
    common_stop_events, initializations, cancellation_events, drop_events = [], [], [], []
    for route in routes:
        decoded = route.get("decoded")
        if decoded and decoded["layout"] in ("rs485_transaction", "rs485_zero"):
            item = {"boot": route["boot"], "event": route["serial"], "record_kind": route["kind"],
                    "evidence": "immutable FIRST pages", **decoded}
            (zeroes if decoded["layout"] == "rs485_zero" else transactions).append(item)
        elif decoded and decoded["layout"] == "rs485_submit_rejection":
            submit_rejections.append({"boot": route["boot"], "event": route["serial"],
                                      "refused_ms": route["at_ms"], **decoded})
        elif decoded and decoded["layout"] in ("rs485_health_transition", "rs485_safety_transition"):
            item = {"boot": route["boot"], "event": route["serial"], "observed_ms": route["at_ms"],
                    "evidence": "immutable FIRST single page", **decoded}
            (health_transitions if decoded["layout"] == "rs485_health_transition" else safety_transitions).append(item)
    # A phase-2 part0 has only 12 words. It must not be joined with part1 from a later sample.
    for observation in observations:
        if observation["source"] == "S485" and observation.get("p") == 2 and len(observation.get("w", [])) >= 16:
            decoded = transaction_decode(observation["w"])
            transactions.append({"boot": observation["boot"], "line": observation["line"],
                                 "evidence": "one periodic observation", **decoded})
    for record in records:
        words = record["fields"].get("w", [])
        hld_event = record["prefix"] == "HLD" and "kind" in record
        if hld_event:
            words = record.get("words", [])
        valid_words = isinstance(words, list) and len(words) == 6 and all(isinstance(word, int) for word in words)
        kind = record.get("kind")
        if valid_words and ((record["prefix"] == "LD" and record["type"] == "EVENT" and kind in (0x3E6, 0x3E7, 0x3E8)) or
                            (hld_event and kind in (6, 7, 8))):
            stop_kind = kind - 0x3E0 if kind >= 0x3E0 else kind
            common_stop_events.append({"boot": record["boot"], "line": record["line"],
                                       "source": record.get("source", "HMI"), "serial": record["serial"],
                                       "observed_ms": record["at_ms"], "kind": kind,
                                       "stage": {6: "first", 7: "upgrade", 8: "complete"}[stop_kind],
                                       "stop_event": words[0], "reason": words[1],
                                       "owner": words[2] >> 16, "requester": words[2] & 0xFFFF,
                                       "request_sequence": words[3], "command": words[4] >> 16,
                                       "deceleration_mm_s2": words[4] & 0xFFFF,
                                       "result": words[5] >> 16, "error": words[5] & 0xFFFF})
            continue
        if valid_words and kind == 0x3F0 and (hld_event or record["type"] == "EVENT"):
            initializations.append({"boot": record["boot"], "line": record["line"],
                                    "source": record.get("source", "HMI"), "observed_ms": record["at_ms"],
                                    "rs485_result": words[0], "modbus_result": words[1], "reserved": words[2:]})
            continue
        if record.get("source") != "S485" or record["type"] != "EVENT" or len(words) != 6 or not all(isinstance(word, int) for word in words):
            continue
        base = {"boot": record["boot"], "line": record["line"], "event": record["serial"], "transaction": words[0]}
        if record.get("kind") == 3:
            valid = words[5]
            durations = {}
            for name, end, start, mask in (("enqueue_to_uart_tc", words[3], words[1], 7),
                                           ("tx_to_uart_tc", words[3], words[2], 6),
                                           ("uart_tc_to_rx_capture", words[4], words[3], 12),
                                           ("enqueue_to_rx_capture", words[4], words[1], 9)):
                durations[name] = unsigned_elapsed(end, start) if valid & mask == mask else None
            wire_events.append({**base, "validity": valid, "times_ms": dict(zip(
                ("enqueue", "tx_start", "uart_tc", "rx_capture"), words[1:5])), "durations_ms": durations})
        elif record.get("kind") == 4:
            status = words[2] & 0xFFFF
            validation_events.append({**base, "epoch": words[1], "motion_owner": words[2] >> 24,
                                      "attempt": (words[2] >> 16) & 255, "status": status,
                                      "status_name": STATUSES[status] if status < len(STATUSES) else "UNKNOWN",
                                      "validity": words[5], "dequeue_to_validation_ms":
                                      unsigned_elapsed(words[4], words[3]) if words[5] & 16 else None})
        elif record.get("kind") == 5:
            consumer_events.append({**base, "consume_ms": words[1], "accepted": bool(words[2] >> 16),
                                    "identity_matched": bool(words[2] & 0xFFFF),
                                    "diagnostic_sidecar_available": bool(words[2] & 0xFFFF),
                                    "business_rejected": not bool(words[2] >> 16),
                                    "association_note": "Accepted business responses remain accepted when the bounded diagnostic sidecar has advanced; unavailable timing is unknown.",
                                    "axis": words[3] >> 16,
                                    "tag": words[3] & 0xFFFF, "epoch": words[4], "status": words[5]})
        elif record.get("kind") == 6:
            cancellation_events.append({**base, "new_epoch": words[1], "safety_mode": words[2],
                                        "motion_owner": words[3], "cancel_ms": words[4], "reserved": words[5]})
        elif record.get("kind") == 7:
            drop_events.append({**base, "request_epoch": words[1], "current_epoch": words[2],
                                "observed_ms": words[3], "safety_mode": words[4], "drop_code": words[5],
                                "drop_reason": {0: "dequeued_stale", 1: "response_queue_full"}.get(words[5], "UNKNOWN")})
        elif record.get("kind") == 0x20:
            speed_requests.append({"boot": record["boot"], "line": record["line"], "event": record["serial"],
                                   "accepted_ms": record["at_ms"], "speed_write_sequence": words[0],
                                   "requested_rpm": signed16(words[1] >> 16),
                                   "first_health_limited_rpm": signed16(words[1]),
                                   "motion_owner": words[2] >> 24, "internal_stop": bool((words[2] >> 16) & 255),
                                   "axis": words[2] & 0xFFFF, "arbiter_generation": words[3],
                                   "previous_acknowledged_rpm": signed16(words[4]),
                                   "internal_can": bool(words[5] >> 16), "degraded_active": bool(words[5] & 0xFFFF)})
        elif record.get("kind") == 0x21:
            can_valid = bool((words[2] >> 16) & 255)
            speed_submissions.append({**base, "submitted_ms": record["at_ms"],
                                      "before_final_health_rpm": signed16(words[1] >> 16),
                                      "submitted_rpm": signed16(words[1]), "motion_owner": words[2] >> 24,
                                      "can_pending_valid": can_valid, "axis": words[2] & 0xFFFF,
                                      "arbiter_generation": words[3], "can_batch": words[4] if can_valid else None,
                                      "speed_write_sequence": words[5]})
        elif record.get("kind") in (0x22, 0x23):
            decoded = rs485_transition_decode(record["kind"], words)
            item = {"boot": record["boot"], "line": record["line"], "event": record["serial"],
                    "observed_ms": record["at_ms"], "evidence": "EVENT", **decoded}
            (health_transitions if record["kind"] == 0x22 else safety_transitions).append(item)

    can_failures = [item for item in routes if (item.get("decoded") or {}).get("layout") == "can-record-failure-v1"]
    can_timing = []
    for observation in observations:
        if observation["source"] not in ("CS", "CA", "CANRAW") or observation.get("p") != 3:
            continue
        words = observation.get("w", [])
        if not isinstance(words, list) or not all(isinstance(word, int) for word in words):
            continue
        item = {"boot": observation["boot"], "line": observation["line"], "sample_ms": observation.get("sample"),
                "source": observation["source"], "part": observation.get("part"), "atomic_group": False}
        if item["part"] == 0 and len(words) == 12:
            item.update(dict(zip(("max_service_gap_ms", "max_tx_result_delay_ms", "max_next_segment_delay_ms",
                                  "max_record_duration_ms", "max_critical_duration_ms"), words[:5])))
            item["max_critical_completion_gaps_ms"] = dict(zip(("11", "25", "13", "53"), words[5:9]))
            item.update(failures=words[9], last_failure_reason=words[10], last_failure_at_ms=words[11])
        elif item["part"] == 1 and len(words) == 7:
            item.update(failure_processed_ms=words[0], tx_result_processed_ms=words[1],
                        record_type=words[2] & 255, frame_index=(words[2] >> 8) & 0xFFFF,
                        record_pending=bool(words[2] & (1 << 24)), record_cancelled=bool(words[2] & (1 << 25)),
                        record_sequence=words[3] & 0xFFFF, record_length=words[3] >> 16,
                        record_started_ms=words[4], record_last_tx_ok_ms=words[5], submitted_frames=words[6])
        else:
            continue
        can_timing.append(item)
    return {"schema": "link5-log-1", "record_count": len(records), "ignored_nonempty_lines": ignored,
            "warnings": warnings, "boots": list(boots.values()), "first_stops": first_stops,
            "common_stop_events": common_stop_events, "initializations": initializations,
            "startup_first": assemble_startup_first(records),
            "can": {"record_failures": can_failures, "timing_observations": can_timing,
                    "stop_wire_identities": can_stop_identities(records)},
            "f103_stop_first": assemble_f103_stop_first(text),
            "route_first": routes, "periodic_observations": observations,
            "periodic_groups": list(periodic_groups.values()),
            "hmi": {"cumulative_tc": timing, "slow_transactions": slow,
                    "max_complete": max(measured, key=lambda item: item["duration_ms"]) if measured else None,
                    "association_note": "E1/E5/EA are retained as raw records. EA has no request ID; adjacent lines are not paired to claim a complete transaction."},
            "rs485": {"transactions": transactions, "zero_evidence": zeroes, "wire_events": wire_events,
                      "validation_events": validation_events, "consumer_events": consumer_events,
                      "submit_rejections": submit_rejections, "speed_requests": speed_requests,
                      "speed_submissions": speed_submissions, "health_transitions": health_transitions,
                      "safety_transitions": safety_transitions, "cancellation_events": cancellation_events,
                      "drop_events": drop_events}, "records": records,
            "limits": ["Sampled or dropped events are not a complete bus history.",
                       "STATE/HEALTH parts and SYNC phases are independent observations even with the same id.",
                       "Missing pages, timestamps or identities remain unknown; no physical cause is inferred.",
                       "Capture-to-UART-TC timings include software capture and preemption, not a physical-wire measurement.",
                       "F103 pages are separated by producer boot or startup banners; legacy logs lacking both require one F103 boot per input."]}


def load_text(path, encoding="auto"):
    raw = Path(path).read_bytes()
    if encoding != "auto":
        return raw.decode(encoding, errors="replace"), encoding
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        return raw.decode("utf-16"), "utf-16"
    if raw[:200].count(b"\x00") > len(raw[:200]) // 4:
        return raw.decode("utf-16-le", errors="replace"), "utf-16-le"
    try:
        return raw.decode("utf-8-sig"), "utf-8-sig"
    except UnicodeDecodeError:
        return raw.decode("gb18030", errors="replace"), "gb18030 (replacement permitted)"


def format_summary(report):
    lines = ["Link5: {} records, {} boot IDs, {} parse warnings".format(
        report["record_count"], len(report["boots"]), len(report["warnings"]))]
    maximum = report["hmi"]["max_complete"]
    if maximum:
        lines.append("HMI observed complete maximum: {:.3f} ms (boot {:X}, line {}, {}); {} 50 ms.".format(
            maximum["duration_ms"], maximum["boot"], maximum["line"], maximum["evidence"],
            "FAIL: reaches/exceeds" if maximum["duration_ms"] >= 50 else "below"))
    else:
        lines.append("HMI complete maximum: UNKNOWN (requires TIMING tc/hz, SLOW stamps/hz or --dwt-hz).")
    for stop in report["first_stops"]:
        lines.append("First stop: boot {:X}, event {:X}, at={} ms, owner={}, requester={}, reason={}, decel={} mm/s^2 (legacy decimal fields).".format(
            stop["boot"], stop["event"], stop.get("at", "?"), stop.get("owner", "?"),
            stop.get("requester", "?"), stop.get("reason", "?"), stop.get("decel", "?")))
    for route in report["route_first"]:
        lines.append("Route FIRST: boot {:X} src={} kind={:X} event={:X}, {}{}".format(
            route["boot"], route["source"], route["kind"], route["serial"],
            "complete" if route["complete"] else "incomplete/unknown layout",
            ", conflicting pages" if route["conflicts"] else ""))
    for startup in report.get("startup_first", []):
        decoded = startup["decoded"]
        lines.append("Startup FIRST: boot {:X}, capture={} ms, {}; source={}, reason={}.".format(
            startup["boot"], startup["at_ms"], "complete" if startup["complete"] else "incomplete/conflicting",
            decoded["source_name"] if decoded else "UNKNOWN", format(decoded["reason"], "04X") if decoded else "UNKNOWN"))
    for failure in report.get("can", {}).get("record_failures", []):
        decoded = failure["decoded"]
        lines.append("CAN record failure: boot {:X}, type={:02X}, seq={}, frame={}, reason={}, elapsed={} ms.".format(
            failure["boot"], decoded["record_type"], decoded["record_sequence"], decoded["frame_index"],
            decoded["failure_name"], decoded["elapsed_at_failure_ms"]))
    can_timing = [item for item in report.get("can", {}).get("timing_observations", []) if item["part"] == 0]
    if can_timing:
        maxima = [max(item[field] for item in can_timing) for field in (
            "max_service_gap_ms", "max_tx_result_delay_ms", "max_next_segment_delay_ms", "max_record_duration_ms")]
        lines.append("CAN observed cumulative maxima: task_gap={} ms, TX_OK_processing={} ms, next_segment={} ms, whole_record={} ms (independent periodic observations).".format(*maxima))
    for stop in report.get("f103_stop_first", []):
        decoded = stop["decoded"]
        lines.append("F103 STOP_FIRST: local at={} ms, {}, source={}, control_age={} ms, axis_age={}.".format(
            stop["at_ms"], "complete" if stop["complete"] else "incomplete/conflicting",
            decoded["source_name"] if decoded else "UNKNOWN", decoded["ctrl_age"] if decoded else "UNKNOWN",
            decoded["axis_age"] if decoded else "UNKNOWN"))
    for zero in report["rs485"]["zero_evidence"]:
        lines.append("RS485 zero: boot {:X} transaction {:X}, previous={} rpm, ACK command={} rpm, raw confirmed={}.".format(
            zero["boot"], zero["transaction"], zero["previous_rpm"], zero["acknowledged_command_rpm"],
            zero["validated_raw_zero_speed"]))
    for rejected in report["rs485"]["submit_rejections"]:
        lines.append("RS485 submit rejection: boot {:X}, at={} ms, status={}, axis={}, metadata valid={}; no enqueue or wire reply.".format(
            rejected["boot"], rejected["refused_ms"], rejected["status_name"], rejected["axis"], rejected["metadata_valid"]))
    health_count, safety_count = len(report["rs485"]["health_transitions"]), len(report["rs485"]["safety_transitions"])
    if health_count or safety_count:
        lines.append("RS485 transitions: {} health, {} safety observations (FIRST/EVENT may describe the same change); electrical cause unknown.".format(
            health_count, safety_count))
    for boot in report["boots"]:
        samples = boot["heap_samples"]
        free = [item["free"] for item in samples if isinstance(item.get("free"), int)]
        stack = [value for value in boot["stack_min_bytes"] if value is not None]
        lines.append("Boot {:X}: heap free observed={}..{} bytes ({} samples); lowest observed task stack={} bytes.".format(
            boot["boot"], min(free) if free else "?", max(free) if free else "?", len(free), min(stack) if stack else "?"))
    lines.append("Periodic records stay independent; IDs are logical groups, not atomic snapshots. Missing evidence remains unknown.")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", metavar="PATH", help="write detailed JSON; '-' prints only JSON to stdout")
    parser.add_argument("--dwt-hz", type=int, help="explicit DWT frequency when absent from the log")
    parser.add_argument("--encoding", default="auto", help="auto, utf-8-sig, gb18030, utf-16, ...")
    args = parser.parse_args(argv)
    if args.dwt_hz is not None and args.dwt_hz <= 0:
        parser.error("--dwt-hz must be positive")
    try:
        text, encoding = load_text(args.log, args.encoding)
        report = analyze_text(text, args.dwt_hz)
        report["input"] = {"path": str(args.log.resolve()), "encoding": encoding}
        if args.json:
            data = json.dumps(report, ensure_ascii=False, indent=2)
            if args.json == "-":
                print(data)
            else:
                Path(args.json).write_text(data + "\n", encoding="utf-8")
        if args.json != "-":
            print(format_summary(report))
        return 0 if report["record_count"] else 2
    except (OSError, UnicodeError, LookupError) as error:
        print("Log analysis failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
