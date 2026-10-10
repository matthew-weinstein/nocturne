import re

import pytest
from pytest_embedded import Dut

BOOT_TIMEOUT_S = 60
CHECK_TIMEOUT_S = 120

BOOT_DONE = "BOOT -> IDLE (boot done)"
FAULT = "BOOT -> FAULT (fault)"
EXPECTED_SEGMENT_FRAMES = [500, 500, 500, 250]

CHECK_LINE = re.compile(rb"NCT_CHECK ([^\r\n]+)")
CAPTURE_SUMMARY = re.compile(rb"peak occupancy (\d+), overflows (\d+)")

def read_check(dut):
    match = dut.expect(CHECK_LINE, timeout=CHECK_TIMEOUT_S)

    fields = {}
    for pair in match.group(1).decode().split():
        key, value = pair.split("=", 1)
        fields[key] = value

    return fields

def test_capture_session(dut: Dut):
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)

    segment_frames = []
    fields = read_check(dut)
    while "result" not in fields:
        assert fields["status"] == "ok", f"segment {fields['segment']} failed its check"
        segment_frames.append(int(fields["frames"]))
        fields = read_check(dut)

    assert fields["result"] == "PASS"
    assert segment_frames == EXPECTED_SEGMENT_FRAMES

    summary = dut.expect(CAPTURE_SUMMARY, timeout=CHECK_TIMEOUT_S)
    peak_occupancy = int(summary.group(1))
    overflows = int(summary.group(2))
    print(f"peak occupancy {peak_occupancy} samples")
    assert overflows == 0

@pytest.mark.manual_setup
def test_fault_without_card(dut: Dut):
    dut.expect_exact(FAULT, timeout=BOOT_TIMEOUT_S)
