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
  input_base = read("src/input/input.cpp")
  output_implementation = read("src/output/output.cpp")
  util_header = read("lib/util.h")
  util_implementation = read("lib/util.cpp")
  service = read("mistserver.service")
  health_service = read("scripts/mist-catchup-health.service")
  health_timer = read("scripts/mist-catchup-health.timer")
  health_installer = read("scripts/install-mist-catchup-health")

  require("checkpointLiveHeader" in header, "InputHLS should declare a live header checkpoint helper")
  require('meta.inputLocalVars["version"] = 5' in implementation,
          "live HLS checkpoints should use metadata schema version 5")
  require('M.inputLocalVars["version"].asInt() < 5' in implementation,
          "live HLS checkpoint loading should reject older metadata schemas")
  require("checkpointLiveHeader" in implementation, "InputHLS should implement live header checkpointing")
  require('config->addOption("liveHeaderCheckpoint"' in implementation,
          "InputHLS should expose a configurable checkpoint interval")
  require("checkpointLiveHeader(false)" in implementation,
          "live parsing should periodically checkpoint after ingesting new segments")
  require("checkpointLiveHeader(true)" in implementation,
          "finish() should force a final checkpoint through the shared helper")
  require("injectLocalVars();" in implementation and ".toFile(" in implementation,
          "checkpoint path should persist injected local HLS playlist state to .dtsh")
  require("writeLiveHeaderCheckpoint" in header,
          "InputHLS should declare a dedicated atomic checkpoint writer")
  require("writeLiveHeaderCheckpoint" in implementation,
          "InputHLS should route checkpoints through the atomic writer")
  require('checkpointPath + ".tmp."' in implementation,
          "checkpoint writer should write to a unique temporary path next to the .dtsh")
  require("fsync(" in implementation,
          "checkpoint writer should fsync the temporary .dtsh before publishing it")
  require("rename(" in implementation,
          "checkpoint writer should atomically rename the temporary .dtsh into place")
  require("remove(" in implementation,
          "checkpoint writer should clean up failed temporary .dtsh writes")

  require("atomicWriteFile" in util_header,
          "libmist should expose complete-buffer atomic file publication")
  require("atomicWriteFile" in util_implementation,
          "libmist should implement complete-buffer atomic file publication")
  require("O_EXCL" in util_implementation and "fsync(" in util_implementation and "rename(" in util_implementation,
          "atomic file publication should use a unique temp, fsync, and rename")
  require("atomicLocalPlaylistWrites" in output_implementation,
          "rolling local playlist output should select the atomic publication path")
  require(output_implementation.count("atomicWriteFile(playlistLocationString, playlistBuffer)") >= 3,
          "initial, per-segment, and final rolling playlist snapshots should publish atomically")
  require("WIFSIGNALED" in input_base and "WTERMSIG" in input_base,
          "input supervision should record the terminating signal instead of a generic -1")

  require("hlsPersistTimeOffset" in header,
          "live HLS should expose the remap-offset persistence helper")
  live_parse = implementation.split("bool InputHLS::parseSegmentAsLive", 1)
  require(len(live_parse) == 2 and "hlsPersistTimeOffset" in live_parse[1].split("void InputHLS::", 1)[0],
          "parseSegmentAsLive should persist the derived remap offset onto the entry")
  require("pageRetryGuard.shouldAttempt" in input_base,
          "bufferFrame should refuse reloading pages that repeatedly fail to fill")
  require("pageRetryGuard.recordFailure" in input_base and "pageRetryGuard.recordSuccess" in input_base,
          "bufferFrame should track per-page load failures and clear them on success")
  require("if (!bufferNext(" in input_base,
          "bufferFrame should abort the page load when a packet cannot be written")
  io_implementation = read("src/io.cpp")
  live_buffer = io_implementation.split("void InOutBase::bufferLivePacket(uint64_t packTime", 1)
  require(len(live_buffer) == 2 and "if (!bufferNext(" in live_buffer[1]
          and "livePage[packTrack].close()" in live_buffer[1],
          "bufferLivePacket must recover the live page when a write is dropped, "
          "instead of dropping every subsequent packet forever")
  require("droppedOnPage" in io_implementation,
          "repeated live-page write failures should be throttled in the logs")
  require('dprintf(out, "<0>")' not in util_implementation,
          "systemd log mapping must not use LOG_EMERG - journald wall-broadcasts it to every terminal")
  require('!strcmp(kind, "FAIL")){dprintf(out, "<2>");}' in util_implementation,
          "FAIL messages should map to LOG_CRIT under systemd logging")
  require("lastAudioKeyTime" in io_implementation and "lastAudioKeyTime" in read("src/io.h"),
          "audio key synthesis must use process-local state - shared ring-tail reads can be "
          "refreshed by a concurrent writer, starving page flips forever")
  require('getInt("lastkeytime", tPages.getEndPos()' not in io_implementation,
          "audio key synthesis must not read the shared pages ring tail record")
  require("live page record not found" in io_implementation,
          "firstkey search misses must be detected instead of silently using record 0")
  clean_shm = read("scripts/mist-clean-stale-shm")
  require("pgrep" in clean_shm.split("pkill", 1)[1] if "pkill" in clean_shm else False,
          "shm cleanup must confirm orphaned Mist processes are gone before removing "
          "their shared memory, or a survivor re-attaches as a second writer")
  require("SuccessExitStatus=75" in health_service,
          "recovered health events should be successful without losing telemetry")
  require("DynamicUser=yes" in health_service and "Wants=mistserver.service" not in health_service,
          "health monitor should be unprivileged and must not start an intentionally stopped server")
  require("Persistent=false" in health_timer and "OnUnitActiveSec=1min" in health_timer,
          "health timer should run every minute without replaying missed checks")
  require("/usr/local/sbin/mist-catchup-health" in health_installer and "systemctl daemon-reload" in health_installer,
          "monitor deployment should install its runtime and reload systemd")

  require("/dev/shm/*Mst*" not in service,
          "systemd unit template must not delete Mist shared-memory metadata on stop")
  require("ExecStartPre=-/usr/local/sbin/mist-clean-stale-shm" in service,
          "systemd unit should clean shared memory left behind by an unclean stop before starting")
  clean_script = read("scripts/mist-clean-stale-shm")
  require("pgrep" in clean_script and "MistController" in clean_script,
          "stale shm cleanup must refuse to run while a controller is alive")
  require("pkill" in clean_script,
          "stale shm cleanup should remove orphaned Mist workers that survived the unit stop")
  require("rm -f /dev/shm/Mst" in clean_script and "sem.Mst" in clean_script,
          "stale shm cleanup should remove leftover Mist pages and semaphores")
  timeout_match = re.search(r"^TimeoutStopSec=(\d+)$", service, re.MULTILINE)
  require(timeout_match is not None, "systemd unit should set TimeoutStopSec explicitly")
  require(int(timeout_match.group(1)) >= 60,
          "systemd unit should allow enough time for clean .dtsh checkpointing")

  print("PASS: live HLS .dtsh checkpoint source checks")


if __name__ == "__main__":
  main()
