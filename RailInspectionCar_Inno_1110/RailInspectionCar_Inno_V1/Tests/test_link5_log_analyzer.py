"""Evidence-level regression fixtures; no firmware or hardware simulation."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("analyze_link5_log", ROOT / "tools/analyze_link5_log.py")
analyzer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analyzer)
FIXTURE = ROOT / "Tests/fixtures/link5_mixed.log"


class Link5LogTests(unittest.TestCase):
    def test_can_stop_identity_uses_exact_wire_scene_and_never_guesses(self):
        text = ("LD 3E8 656 1 EVENT src=CS kind=100 w=3E8,3E9,507,1234,0,0\n"
                "LD 3EC 656 2 EVENT src=CS kind=10C w=3E8,3EB,9,1100007,5,3\n")
        item = analyzer.analyze_text(text)["can"]["stop_wire_identities"][0]
        self.assertEqual((item["stop_event"], item["can_id"], item["raw_command"], item["raw_parameter"]), (9, 0x110, 5, 0))
        self.assertEqual(item["raw_lines"], [1])
        missing = analyzer.analyze_text(text.splitlines()[1])["can"]["stop_wire_identities"][0]
        self.assertFalse(missing["payload_available"])
        self.assertIsNone(missing["raw_command"])
        other_boot = text.replace("3E8 656 1", "3E8 655 1")
        self.assertFalse(analyzer.analyze_text(other_boot)["can"]["stop_wire_identities"][0]["payload_available"])

    def test_can_record_failure_exact_pages_and_independent_timing_parts(self):
        words = [1200, 5 | (0x25 << 8) | (26 << 16), 99 | (104 << 16), 0xABC,
                 1000, 995, 1001, 1190, 1194, 1200, 3, 100, 3, 0, 9, 7]
        text = "".join("LD 4B0 656 1 FIRST src=CS kind=111 p={} w={}\n".format(
            part, ",".join(format(word, "X") for word in words[8*part:8*part+8])) for part in range(2))
        text += "LD 500 656 0 HEALTH src=CS p=3 part=0 id=1 sample=500 w=9,7,5,C8,64,96,AA,BB,CC,1,5,4B0\n"
        text += "LD 510 656 0 HEALTH src=CS p=3 part=1 id=1 sample=510 w=4B0,4AA,1001A25,680063,3E8,4A6,AA\n"
        report = analyzer.analyze_text(text)
        failure = report["can"]["record_failures"][0]["decoded"]
        self.assertEqual((failure["record_type"], failure["frame_index"], failure["record_sequence"],
                          failure["record_length"]), (0x25, 26, 99, 104))
        self.assertEqual(failure["failure_name"], "record_or_payload_deadline")
        self.assertEqual(failure["elapsed_at_failure_ms"], 200)
        observations = report["can"]["timing_observations"]
        self.assertEqual([item["sample_ms"] for item in observations], [0x500, 0x510])
        self.assertFalse(any(item["atomic_group"] for item in observations))
        self.assertEqual(observations[0]["max_next_segment_delay_ms"], 5)
        self.assertNotIn("record_sequence", observations[0])
        self.assertNotIn("max_next_segment_delay_ms", observations[1])
        self.assertEqual(observations[1]["record_sequence"], 99)
        partial = analyzer.analyze_text(text.splitlines()[0])
        self.assertEqual(partial["can"]["record_failures"], [])
        self.assertFalse(partial["route_first"][0]["complete"])

    def test_f103_first_stop_preserves_decimal_hex_and_missing_page_limits(self):
        pages = [
            "STOP_FIRST v=1 at=1000 part=0 source=1 recovery=7 detail=116 state=5 event_at=999 key=2/04 boot=00000656/DEADBEEF token=100/23 target=-400/-1453\n",
            "STOP_FIRST v=1 at=1000 part=1 valid=0F ctrl_age=500 axis_age=99/100 hb_age=22 ctrl_seq=123 speed_seq=456 gen=9 flags=0000040C owner=4 life=3 axes=3\n",
            "STOP_FIRST v=1 at=1000 part=2 axis_flags=9F/9F mode=2/2 fault=0/0 merged=2\n"]
        report = analyzer.analyze_text("noise\n" + "".join(pages))
        stop = report["f103_stop_first"][0]
        self.assertTrue(stop["complete"])
        decoded = stop["decoded"]
        self.assertEqual(decoded["source_name"], "AUTO_CONTROL_STALE")
        self.assertEqual(decoded["boot"], [0x656, 0xDEADBEEF])
        self.assertEqual(decoded["target"], [-400, -1453])
        self.assertEqual(decoded["axis_age"], [99, 100])
        self.assertEqual(decoded["key"], [2, 4])
        self.assertEqual(decoded["ctrl_age"], 500)
        self.assertEqual(decoded["flags"], 0x40C)
        self.assertIn("F103 STOP_FIRST", analyzer.format_summary(report))
        incomplete = analyzer.analyze_text(pages[0] + pages[2])["f103_stop_first"][0]
        self.assertFalse(incomplete["complete"])
        self.assertIsNone(incomplete["decoded"])
        self.assertEqual(incomplete["missing_pages"], [1])
        conflicting = analyzer.analyze_text("".join(pages) + pages[1].replace("ctrl_age=500", "ctrl_age=501"))
        self.assertFalse(conflicting["f103_stop_first"][0]["complete"])
        retry = analyzer.analyze_text("".join(pages) + pages[2].replace("merged=2", "merged=3"))
        self.assertTrue(retry["f103_stop_first"][0]["complete"])
        # Local decision times may repeat on a subsequent MCU startup. The
        # previous incomplete first must never acquire another boot's pages.
        banner = "UPPER CAN SIM V1.1 MOTION TEST CANAGE_R1 boot=DEADBEEF USART1=115200\n"
        reboot = banner.replace("DEADBEEF", "CAFEBABE")
        separate = analyzer.analyze_text(banner + pages[0] + reboot + "".join(pages[1:]))["f103_stop_first"]
        self.assertEqual(len(separate), 2)
        self.assertTrue(all(not group["complete"] for group in separate))
        explicit_pages = [page.rstrip() + " upper_boot=DEADBEEF\n" for page in pages]
        separate = analyzer.analyze_text(explicit_pages[0] + explicit_pages[1].replace("DEADBEEF", "CAFEBABE") +
                                         explicit_pages[2].replace("DEADBEEF", "CAFEBABE"))["f103_stop_first"]
        self.assertEqual(len(separate), 2)
        self.assertTrue(all(not group["complete"] for group in separate))
        self.assertTrue(analyzer.analyze_text("".join(explicit_pages))["f103_stop_first"][0]["complete"])
        malformed = analyzer.analyze_text(pages[0].replace("boot=00000656/DEADBEEF", "boot=00000656"))["f103_stop_first"]
        self.assertIsNone(malformed[0]["at_ms"])

    @classmethod
    def setUpClass(cls):
        cls.text = FIXTURE.read_text(encoding="utf-8")
        cls.report = analyzer.analyze_text(cls.text)

    def test_legacy_first_fields_are_decimal_despite_hex_headers(self):
        stop = self.report["first_stops"][0]
        self.assertEqual((stop["boot"], stop["event"]), (0x634, 0x10))
        self.assertEqual((stop["at"], stop["reason"], stop["seq"], stop["cmd"], stop["decel"], stop["gen"]),
                         (100, 10, 16, 128, 600, 19))
        boot = self.report["boots"][0]
        self.assertEqual(boot["boot_records"][0]["lease"], 1000)
        self.assertEqual(boot["heap_samples"][0]["free"], 7344)
        self.assertEqual(boot["stack_min_bytes"], [512, 508, None, 1024, 256])

    def test_noise_and_same_physical_line_and_kind_above_ff(self):
        self.assertEqual(self.report["ignored_nonempty_lines"], 1)
        events = [record for record in self.report["records"] if record.get("kind") == 0xABC]
        self.assertEqual({record["prefix"] for record in events}, {"LD", "HLD"})
        event = next(record for record in self.report["records"] if record.get("kind") == 0x203)
        self.assertEqual(event["fields"]["w"][3], 0x123)
        self.assertEqual(event["record_in_line"], 0)

    def test_first_pages_are_boot_scoped_and_conflicts_are_not_hidden(self):
        routes = self.report["route_first"]
        complete = next(item for item in routes if item["source"] == "S485" and item["kind"] == 0x12 and item["boot"] == 0x634)
        self.assertTrue(complete["complete"])
        partial = next(item for item in routes if item["boot"] == 0x635)
        self.assertFalse(partial["complete"])
        self.assertEqual(partial["missing_pages"], [1])
        self.assertIsNone(partial["words"])
        conflict = next(item for item in routes if item["source"] == "CA")
        self.assertEqual(conflict["kind"], 0x201)
        self.assertTrue(conflict["conflicts"])
        self.assertFalse(conflict["complete"])

    def test_periodic_parts_and_sync_phases_are_not_joined(self):
        rf = next(group for group in self.report["periodic_groups"] if group["source"] == "RF")
        self.assertFalse(rf["atomic"])
        self.assertEqual([part["sample"] for part in rf["observations"]], [0x100, 0x105])
        sync = next(group for group in self.report["periodic_groups"] if group["source"] == "SYNC")
        self.assertEqual({part["p"] for part in sync["observations"]}, {0, 1})
        # The split phase-2 words look like a full transaction if concatenated;
        # they are sampled separately and must not manufacture a second transaction.
        self.assertEqual(len(self.report["rs485"]["transactions"]), 1)

    def test_actual_tc_maximum_and_dwt_wrap(self):
        hmi = self.report["hmi"]
        self.assertAlmostEqual(hmi["cumulative_tc"][0]["max_ms"], 48.0)
        self.assertAlmostEqual(hmi["slow_transactions"][0]["duration_ms"], 50.0)
        self.assertAlmostEqual(hmi["max_complete"]["duration_ms"], 50.0)
        self.assertIn("actual-TC", hmi["max_complete"]["evidence"])
        self.assertIn("FAIL", analyzer.format_summary(self.report))
        records = "HLD 10 1 1 E1 460,3,60,100,200,10\nHLD 11 1 2 E5 460,3,C5,1,100,300\nHLD 30 1 3 EA 600C5,1,30,400,1,0\n"
        self.assertIsNone(analyzer.analyze_text(records, 168000000)["hmi"]["max_complete"])

    def test_rs485_stage_timing_validity_and_tick_wrap(self):
        transaction = self.report["rs485"]["transactions"][0]
        self.assertEqual(transaction["durations_ms"]["queue_wait"], 4)
        self.assertEqual(transaction["durations_ms"]["tx_to_uart_tc"], 10)
        self.assertEqual(transaction["durations_ms"]["enqueue_to_validation"], 28)
        self.assertEqual(transaction["motion_owner"], 2)
        self.assertEqual(transaction["protocol_owner"], 1)
        wire = self.report["rs485"]["wire_events"][0]
        self.assertEqual(wire["durations_ms"]["enqueue_to_uart_tc"], 18)
        words = [0] * 16
        words[2:9] = [10, 20, 30, 0, 0, 50, 0]
        words[12] = 0x03040011  # failure validated, no TX or RX or consumer stamps
        decoded = analyzer.transaction_decode(words)
        self.assertIsNone(decoded["durations_ms"]["tx_to_uart_tc"])
        self.assertIsNone(decoded["durations_ms"]["rx_capture_to_validation"])
        self.assertEqual(decoded["status_name"], "TIMEOUT")
        self.assertEqual(decoded["durations_ms"]["enqueue_to_validation"], 40)

    def test_zero_requires_complete_crc_bearing_reply_and_validity(self):
        zero = self.report["rs485"]["zero_evidence"][0]
        self.assertEqual((zero["previous_rpm"], zero["acknowledged_command_rpm"]), (600, -600))
        self.assertEqual(zero["raw_prefix_hex"], "0103020000b844")
        self.assertTrue(zero["validated_raw_zero_speed"])
        route = next(item for item in self.report["route_first"] if item["source"] == "S485" and item["kind"] == 0x10)
        words = route["words"].copy()
        words[15] ^= 0x100
        self.assertFalse(analyzer.zero_decode(words)["validated_raw_zero_speed"])
        words = route["words"].copy()
        words[13] = 0x00000B00
        decoded = analyzer.zero_decode(words)
        self.assertFalse(decoded["validated_raw_zero_speed"])
        self.assertTrue(decoded["firmware_zero_hook"])

    def test_submit_rejection_is_complete_with_exactly_one_six_word_page(self):
        report = analyzer.analyze_text("LD 123 634 11 FIRST src=S485 kind=14 p=0 merged=0 w=44,3,30011,FF010001,01060603,100C8\n"
                                       "LD 124 634 12 FIRST src=S485 kind=14 p=0 merged=0 w=FFFFFFFF,3,1FFFF,FFFFFF00,FFFFFFFF,FFFFFFFF\n")
        self.assertEqual(len(report["rs485"]["submit_rejections"]), 2)
        route = report["route_first"][0]
        self.assertTrue(route["complete"])
        self.assertEqual(route["expected_words"], 6)
        self.assertEqual(route["missing_pages"], [])
        rejected = report["rs485"]["submit_rejections"][0]
        self.assertEqual(rejected["refused_ms"], 0x123)
        self.assertEqual(rejected["status_name"], "QUEUE_FULL")
        self.assertEqual((rejected["transaction"], rejected["axis"], rejected["function"], rejected["address"],
                          rejected["requested_value"]), (0x44, 0, 6, 0x603, 200))
        self.assertIsNone(rejected["motion_owner"])
        self.assertIsNone(rejected["enqueue_ms"])
        self.assertFalse(rejected["raw_wire_reply_available"])
        null = report["rs485"]["submit_rejections"][1]
        self.assertFalse(null["metadata_valid"])
        self.assertIsNone(null["transaction"])
        self.assertIsNone(null["address"])
        self.assertIsNone(null["protocol_owner"])
        self.assertIn("submit rejection", analyzer.format_summary(report))

    def test_short_or_missing_or_extra_submit_rejection_page_is_not_decoded(self):
        report = analyzer.analyze_text("LD 1 1 1 FIRST src=S485 kind=14 p=0 w=1,2,3,4,5\n"
                                       "LD 2 1 2 FIRST src=S485 kind=14 p=1 w=1,2,3,4,5,6\n"
                                       "LD 3 1 3 FIRST src=S485 kind=14 p=0 w=1,2,3,4,5,6,7\n")
        self.assertEqual(report["rs485"]["submit_rejections"], [])
        self.assertTrue(all(not route["complete"] and route["decoded"] is None for route in report["route_first"]))
        self.assertEqual(report["route_first"][1]["missing_pages"], [0])

    def test_request_and_final_health_limits_decode_signed_values_and_can_validity(self):
        report = analyzer.analyze_text("LD 10 1 1 EVENT src=S485 kind=20 w=9,FF38FF9C,05000001,3,FF38,10001\n"
                                       "LD 11 1 2 EVENT src=S485 kind=21 w=44,FF9CFFCE,05010001,3,22,9\n"
                                       "LD 12 1 3 EVENT src=S485 kind=21 w=45,640032,02000000,4,22,A\n")
        request = report["rs485"]["speed_requests"][0]
        self.assertEqual((request["requested_rpm"], request["first_health_limited_rpm"],
                          request["previous_acknowledged_rpm"]), (-200, -100, -200))
        self.assertEqual((request["axis"], request["speed_write_sequence"], request["motion_owner"]), (1, 9, 5))
        self.assertTrue(request["internal_can"])
        self.assertTrue(request["degraded_active"])
        submitted = report["rs485"]["speed_submissions"][0]
        self.assertEqual((submitted["before_final_health_rpm"], submitted["submitted_rpm"],
                          submitted["speed_write_sequence"], submitted["can_batch"]), (-100, -50, 9, 0x22))
        self.assertIsNone(report["rs485"]["speed_submissions"][1]["can_batch"])

    def test_health_transition_flags_failures_identity_and_invalid_age(self):
        old = 0x80000000 | (1 << 16) | (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21)
        new = 0x80000042 | (1 << 17) | (1 << 19) | (1 << 21) | (1 << 22) | (1 << 23) | (2 << 24) | (1 << 26) | (1 << 27)
        report = analyzer.analyze_text("LD 1 1 1 EVENT src=S485 kind=22 w=05000001,{:X},{:X},01020304,55,64\n"
                                       "LD 2 1 2 FIRST src=S485 kind=22 p=0 merged=0 w=05000001,{:X},{:X},01020304,55,64\n".format(old, new, old, new))
        self.assertTrue(report["route_first"][0]["complete"])
        self.assertEqual(report["route_first"][0]["expected_words"], 6)
        self.assertEqual(len(report["rs485"]["health_transitions"]), 2)
        transition = report["rs485"]["health_transitions"][0]
        self.assertEqual((transition["axis"], transition["motion_owner"], transition["item_name"],
                          transition["feedback_transaction"]), (1, 5, "speed", 0x55))
        self.assertEqual((transition["feedback_stop_pending"], transition["enable_failures"],
                          transition["fault_failures"], transition["speed_failures"]), (1, 2, 3, 4))
        self.assertFalse(transition["after"]["enable_feedback"])
        self.assertEqual(transition["after"]["fault_code"], 0x42)
        self.assertEqual(transition["after"]["health"], 2)
        self.assertTrue(transition["after"]["controlled_stop"])
        self.assertTrue(transition["after"]["offline"])
        self.assertEqual(transition["stored_tick_age_ms"], 100)
        self.assertFalse(transition["feedback_sample_valid"])
        self.assertIsNone(transition["valid_feedback_age_ms"])
        self.assertIsNone(transition["physical_root_cause"])
        valid = analyzer.rs485_transition_decode(0x22, [0x02010000, 0, old, 0, 0x77, 25])
        self.assertEqual(valid["item_name"], "fault")
        self.assertTrue(valid["baseline_observation"])
        self.assertEqual(valid["valid_feedback_age_ms"], 25)

    def test_safety_transition_full_failure_and_known_six_word_layouts(self):
        report = analyzer.analyze_text("LD 1 1 1 FIRST src=S485 kind=23 p=0 w=80000001,8001AB03,DEADBEEF,123,55,03020104\n"
                                       "LD 2 1 2 EVENT src=S485 kind=23 w=80000001,8001AB03,DEADBEEF,123,55,03020104\n")
        self.assertTrue(report["route_first"][0]["complete"])
        self.assertEqual(len(report["rs485"]["safety_transitions"]), 2)
        transition = report["rs485"]["safety_transitions"][0]
        self.assertEqual(transition["full_failure"], 0xDEADBEEF)
        self.assertEqual(transition["after"]["phase"], 3)
        self.assertEqual(transition["after"]["failed_task"], 0xAB)
        self.assertTrue(transition["after"]["boot_locked"])
        self.assertFalse(transition["baseline_observation"])
        self.assertEqual((transition["parameters_mask"], transition["zero_mask"],
                          transition["pending_axis"], transition["pending_step"]), (3, 2, 1, 4))
        self.assertIsNone(transition["physical_root_cause"])
        for kind in (0x14, 0x22, 0x23):
            for page, words in ((0, "1,2,3,4,5"), (1, "1,2,3,4,5,6"), (0, "1,2,3,4,5,6,7")):
                partial = analyzer.analyze_text("LD 1 1 1 FIRST src=S485 kind={:X} p={} w={}\n".format(kind, page, words))
                route = partial["route_first"][0]
                self.assertEqual(route["expected_words"], 6)
                self.assertFalse(route["complete"])
                self.assertIsNone(route["decoded"])

    def test_common_stop_namespace_does_not_collide_with_rs485_drop_cancel(self):
        stops = self.report["common_stop_events"]
        self.assertEqual(len(stops), 1)
        self.assertEqual(stops[0]["kind"], 0x3E7)
        self.assertEqual((stops[0]["source"], stops[0]["stage"], stops[0]["stop_event"]), ("S485", "upgrade", 0x10))
        self.assertEqual((stops[0]["owner"], stops[0]["requester"], stops[0]["command"],
                          stops[0]["deceleration_mm_s2"], stops[0]["result"], stops[0]["error"]), (2, 1, 0x100, 600, 2, 3))
        drops = self.report["rs485"]["drop_events"]
        self.assertEqual(len(drops), 1)
        self.assertEqual((drops[0]["transaction"], drops[0]["drop_reason"]), (0x55, "response_queue_full"))
        self.assertEqual(self.report["initializations"][0]["rs485_result"], 0)
        self.assertEqual(self.report["initializations"][0]["modbus_result"], 0)
        report = analyzer.analyze_text("HLD 1 1 1 E6 10,5,10002,9,1000258,0\n"
                                       "HLD 2 1 2 E8 10,5,10002,9,1000258,30000\n"
                                       "LD 3 1 3 EVENT src=S485 kind=6 w=55,3,1,2,3,0\n")
        self.assertEqual([item["stage"] for item in report["common_stop_events"]], ["first", "complete"])
        self.assertEqual(len(report["rs485"]["cancellation_events"]), 1)
        self.assertEqual(report["rs485"]["drop_events"], [])
        self.assertEqual(report["rs485"]["transactions"], [])

    def test_unknown_layout_and_malformed_fields_remain_unknown(self):
        report = analyzer.analyze_text("LD 1 1 1 FIRST src=NEW kind=ABC p=0 w=1,2,3\n"
                                       "LD 2 1 2 FIRST src=RF kind=40 p=0 w=NA\n"
                                       "LD 3 1 3 EVENT src=CA kind=ZZZ w=1,2\n")
        route = report["route_first"][0]
        self.assertIsNone(route["expected_words"])
        self.assertIsNone(route["decoded"])
        self.assertTrue(report["warnings"])

    def test_startup_first_reassembles_only_exact_complete_pages(self):
        words = [0] * 24
        words[0:4] = [0x53540118, 0x02010002, 0x10000, 1538]
        words[8:10] = [27, 0x0001000B]
        words[13] = 0xFF9C0000
        words[14:17] = [499, 0xFFFFFFFF, 20]
        def page(part, body=words, serial=1, at=1538):
            return "LD {:X} 643 {:X} STARTUP part={:X} parts=3 count=18 merged=0 w={}\n".format(
                at, serial, part, ",".join(format(value, "X") for value in body[part*8:part*8+8]))
        text = page(2) + page(0) + page(1) + page(1)
        report = analyzer.analyze_text(text)
        capture = report["startup_first"][0]
        self.assertTrue(capture["complete"])
        self.assertEqual(capture["words"], words)
        decoded = capture["decoded"]
        self.assertEqual(decoded["source_name"], "BOARD_KEY")
        self.assertEqual(decoded["key"], {"index": 1, "event_type": 2, "sampled_level": 0})
        self.assertEqual(decoded["axes"][0]["ages_ms"], {"speed": 499, "fault": None, "enable": 20})
        self.assertEqual(decoded["axes"][1]["actual_speed_rpm"], -100)
        self.assertIn("Startup FIRST:", analyzer.format_summary(report))
        partial = analyzer.analyze_text(page(0) + page(2))["startup_first"][0]
        self.assertFalse(partial["complete"])
        self.assertEqual(partial["missing_pages"], [1])
        self.assertIsNone(partial["decoded"])
        changed = words.copy()
        changed[8] = 28
        conflict = analyzer.analyze_text(text + page(1, changed))["startup_first"][0]
        self.assertFalse(conflict["complete"])
        self.assertTrue(conflict["conflicts"])
        self.assertIsNone(conflict["decoded"])
        separate = analyzer.analyze_text(page(0) + page(1, at=1540) + page(2, serial=2))
        self.assertEqual(len(separate["startup_first"]), 3)
        self.assertTrue(all(not group["complete"] for group in separate["startup_first"]))

    def test_startup_bad_layout_and_consumer_sidecar_are_not_business_faults(self):
        malformed = analyzer.analyze_text("LD 1 1 1 STARTUP part=0 parts=3 count=18 w=1\n")
        self.assertFalse(malformed["startup_first"][0]["complete"])
        self.assertIsNone(malformed["startup_first"][0]["decoded"])
        words = [0] * 24
        self.assertIsNone(analyzer.startup_decode(words))
        report = analyzer.analyze_text("LD 1 1 1 EVENT src=S485 kind=5 w=42,10,10000,1,2,0\n"
                                       "LD 2 1 2 EVENT src=S485 kind=5 w=43,11,1,1,2,4\n")
        accepted, refused = report["rs485"]["consumer_events"]
        self.assertFalse(accepted["diagnostic_sidecar_available"])
        self.assertFalse(accepted["business_rejected"])
        self.assertTrue(refused["business_rejected"])

    def test_cli_json_and_utf16_and_missing_frequency(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "log.txt"
            log.write_text(self.text, encoding="utf-16")
            output = Path(directory) / "result.json"
            result = subprocess.run([sys.executable, "-B", str(ROOT / "tools/analyze_link5_log.py"),
                                     str(log), "--json", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["input"]["encoding"], "utf-16")
            self.assertEqual(report["record_count"], self.report["record_count"])
        report = analyzer.analyze_text("HLD 1 1 0 TIMING 1,2,3,4,0 tc=10/20\n")
        self.assertIsNone(report["hmi"]["max_complete"])
        report = analyzer.analyze_text("HLD 1 1 0 TIMING 1,2,3,4,0 tc=10/20\n", 1000)
        self.assertEqual(report["hmi"]["max_complete"]["duration_ms"], 32.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
