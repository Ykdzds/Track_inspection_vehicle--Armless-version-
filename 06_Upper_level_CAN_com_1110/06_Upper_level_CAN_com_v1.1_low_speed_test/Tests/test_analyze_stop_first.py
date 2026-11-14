import unittest
from analyze_stop_first import analyze


class StopFirstDecoderTests(unittest.TestCase):
    def test_pages_noise_missing_and_new_boot(self):
        data = "noise STOP_FIRST v=1 at=1001 part=0 source=1 boot=000001C8/0000007B token=17/23\n"
        data += "STOP_FIRST v=1 at=1001 part=1 ctrl_age=500 axis_age=0/0\n"
        data += "STOP_FIRST v=1 at=1001 part=2 merged=3\n"
        data += "STOP_FIRST v=1 at=1001 part=0 source=2 boot=000001C8/0000007C token=18/24\n"
        result = analyze(data)
        self.assertEqual(len(result["events"]), 2)
        self.assertEqual(result["events"][0]["missing_pages"], [])
        self.assertEqual(result["events"][0]["source"], "CTRL_STALE")
        self.assertEqual(result["events"][1]["missing_pages"], [1, 2])
        self.assertEqual(result["events"][1]["source"], "AXIS_STALE")

    def test_orphan_and_invalid_page(self):
        result = analyze("STOP_FIRST v=1 at=3 part=1 ctrl_age=500\nSTOP_FIRST v=1 at=3 part=9\n")
        self.assertEqual(result["events"][0]["missing_pages"], [0, 2])
        self.assertEqual(result["events"][0]["source"], "UNKNOWN")
        self.assertEqual(len(result["warnings"]), 1)
        result = analyze("STOP_FIRST v=1 at=3 part=0 source=?\n")
        self.assertEqual(result["events"][0]["source"], "UNKNOWN")
        self.assertEqual(len(result["warnings"]), 1)


if __name__ == "__main__":
    unittest.main()
