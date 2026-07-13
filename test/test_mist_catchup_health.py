#!/usr/bin/env python3
"""Focused behavioral tests for the deployed catch-up health monitor."""

from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import runpy
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODULE = runpy.run_path(str(ROOT / "scripts" / "mist-catchup-health"))


class CatchupHealthTests(unittest.TestCase):
    def setUp(self) -> None:
        self.module_globals = MODULE["read_stable_snapshot"].__globals__
        self.old_gap = self.module_globals["SNAPSHOT_GAP"]
        self.module_globals["SNAPSHOT_GAP"] = 0.001

    def tearDown(self) -> None:
        self.module_globals["SNAPSHOT_GAP"] = self.old_gap

    def test_stable_snapshot_requires_complete_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.m3u8"
            path.write_text("#EXTM3U\n#EXT-X-PROGRAM-DATE-TIME:2026-07-12T10:00:00Z\n")
            self.assertTrue(MODULE["read_stable_snapshot"](path).startswith("#EXTM3U"))
            path.write_text("#EXTM3U\n#EXT-X-PROGRAM-DATE-TIME:2026-0")
            with self.assertRaises(MODULE["SnapshotUnstable"]):
                MODULE["read_stable_snapshot"](path)

    def test_first_failure_then_success_is_recovered(self) -> None:
        attempts = iter((RuntimeError("transient"), "healthy"))

        def operation():
            value = next(attempts)
            if isinstance(value, Exception):
                raise value
            return value

        output = io.StringIO()
        with redirect_stdout(output):
            value, recovered = MODULE["confirmed_check"]("fox", "served", (0.0,), operation)
        self.assertEqual(value, "healthy")
        self.assertTrue(recovered)
        events = [json.loads(line)["event"] for line in output.getvalue().splitlines()]
        self.assertEqual(events, ["attempt_failed", "recovered"])

    def test_two_failures_are_confirmed(self) -> None:
        output = io.StringIO()

        def operation():
            raise RuntimeError("persistent")

        with redirect_stdout(output):
            with self.assertRaises(MODULE["ConfirmedFailure"]):
                MODULE["confirmed_check"]("cbs", "served", (0.0,), operation)
        events = [json.loads(line)["event"] for line in output.getvalue().splitlines()]
        self.assertEqual(events, ["attempt_failed", "attempt_failed", "confirmed_failure"])

    def test_atomic_replacements_are_stable_snapshots(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.m3u8"
            generations = [
                "#EXTM3U\n#GENERATION:A\n#EXT-X-PROGRAM-DATE-TIME:2026-07-12T10:00:00Z\n",
                "#EXTM3U\n#GENERATION:B\n#EXT-X-PROGRAM-DATE-TIME:2026-07-12T10:00:04Z\n",
            ]
            for index in range(20):
                temp = path.with_name(f"{path.name}.tmp.{index}")
                temp.write_text(generations[index % 2])
                os.replace(temp, path)
                snapshot = MODULE["read_stable_snapshot"](path)
                self.assertIn(snapshot, generations)

    def test_variant_must_preserve_exact_catchup_query(self) -> None:
        original_fetch = self.module_globals["fetch"]

        def fake_fetch(stage, url, **kwargs):
            self.assertEqual(stage, "master")
            return b"#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n/hls/fox_catchup/1/index.m3u8\n"

        self.module_globals["fetch"] = fake_fetch
        try:
            with self.assertRaises(MODULE["HealthError"]) as caught:
                MODULE["validate_playback_once"]("fox", None)
            self.assertEqual(caught.exception.stage, "variant_query")
        finally:
            self.module_globals["fetch"] = original_fetch

    def test_logged_urls_strip_tokens_and_credentials(self) -> None:
        value = MODULE["safe_url"](
            "https://user:pass@example.com/hls/index.m3u8?startunix=-20&catchup=1&tkn=secret"
        )
        self.assertEqual(
            value, "https://example.com/hls/index.m3u8?startunix=-20&catchup=1"
        )


    def test_catchup_shift_is_configurable_and_clears_ingest_latency(self) -> None:
        """HLS-pull ingest adds ~20-26s of latency; probing startunix=-20 lands
        at the ingest edge and sees a near-empty playlist. The probe shift must
        be configurable and default beyond that latency."""
        self.assertIn("CATCHUP_SHIFT", MODULE)
        self.assertEqual(MODULE["CATCHUP_SHIFT"], 40)
        source = (ROOT / "scripts" / "mist-catchup-health").read_text(encoding="utf-8")
        self.assertNotIn("startunix=-20", source)
        self.assertIn("MIST_CATCHUP_SHIFT", source)



if __name__ == "__main__":
    unittest.main()
