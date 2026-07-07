#!/usr/bin/env python3
"""Regression checks for live HLS catch-up restart persistence.

These checks intentionally inspect source/configuration text instead of trying to
boot a full MistServer instance in CI. The bug this guards against was a
combination of two implementation details:

* live HLS catch-up metadata was only written to .dtsh during a clean input
  shutdown, leaving stale state after long-running streams or fast restarts;
* the systemd service template removed Mist shared-memory files after stop,
  throwing away the only fresh live metadata cache before the next startup.
"""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]


def read(rel_path: str) -> str:
  return (ROOT / rel_path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
  if not condition:
    print(f"FAIL: {message}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
  header = read("src/input/input_hls.h")
  implementation = read("src/input/input_hls.cpp")
  service = read("mistserver.service")

  require("checkpointLiveHeader" in header, "InputHLS should declare a live header checkpoint helper")
  require("checkpointLiveHeader" in implementation, "InputHLS should implement live header checkpointing")
  require('config->addOption("liveHeaderCheckpoint"' in implementation,
          "InputHLS should expose a configurable checkpoint interval")
  require("checkpointLiveHeader(false)" in implementation,
          "live parsing should periodically checkpoint after ingesting new segments")
  require("checkpointLiveHeader(true)" in implementation,
          "finish() should force a final checkpoint through the shared helper")
  require("injectLocalVars();" in implementation and ".toFile(" in implementation,
          "checkpoint path should persist injected local HLS playlist state to .dtsh")

  require("/dev/shm/*Mst*" not in service,
          "systemd unit template must not delete Mist shared-memory metadata on stop")
  timeout_match = re.search(r"^TimeoutStopSec=(\d+)$", service, re.MULTILINE)
  require(timeout_match is not None, "systemd unit should set TimeoutStopSec explicitly")
  require(int(timeout_match.group(1)) >= 60,
          "systemd unit should allow enough time for clean .dtsh checkpointing")

  print("PASS: live HLS .dtsh checkpoint source checks")


if __name__ == "__main__":
  main()
