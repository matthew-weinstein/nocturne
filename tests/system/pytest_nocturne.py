import re

import pytest
from pytest_embedded import Dut

BOOT_TIMEOUT_S = 60
CAPTURE_TIMEOUT_S = 60
CHECK_TIMEOUT_S = 120
UPLOAD_TIMEOUT_S = 120

BOOT_DONE = "BOOT -> IDLE (boot done)"
RECORDING_STARTED = "IDLE -> RECORDING (short press)"
HEALTH_OK = "health check ok"
HEALTH_FAILED = "health check failed"
RECORDING_STOPPED = "RECORDING -> FINALIZING (long press)"
RECORDING_FAILED = "RECORDING -> FINALIZING (recording failed)"
FINALIZED = "FINALIZING -> IDLE (finalize ok)"
FINALIZE_FAILED = "FINALIZING -> IDLE (finalize failed)"
CARD_FULL_REFUSED = "short press refused in IDLE: card full"
FAULT = "BOOT -> FAULT (fault)"
FAULT_DONE = "NCT_FAULT done"
WIFI_DOWN = "NCT_WIFI outage=start status=ESP_OK"
WIFI_UP = "NCT_WIFI outage=end status=ESP_OK"

FULL_SEGMENT_FRAMES = 500
EXPECTED_FRAMES = 1750
FRAME_TOLERANCE = 50
EXPECTED_MUTED_FRAMES = 165
MUTED_FRAME_TOLERANCE = 30

PROBLEM_BUFFER_OVERFLOW = 0x08
PROBLEM_MIC_SILENT = 0x10
PROBLEM_UPLOAD_BACKLOG = 0x20
PROBLEM_SD_WRITE_ERRORS = 0x40
PROBLEM_AUDIO_DROPPED = 0x80
MAX_UPLOAD_QUEUE_DEPTH = 20

CHECK_LINE = re.compile(rb"NCT_CHECK ([^\r\n]+)")
UPLOAD_LINE = re.compile(rb"NCT_UPLOAD ([^\r\n]+)")
CAPTURE_SUMMARY = re.compile(rb"peak occupancy (\d+), overflows (\d+)")
HEALTH_LINE = re.compile(rb"upload queue (\d+), wifi (\w+), problems 0x([0-9a-f]+)")
RECORDING_OR_DONE = re.compile(rb"IDLE -> RECORDING|NCT_FAULT done")

def fault_build(fault):
    return pytest.mark.parametrize("build_dir", [f"build-fault-{fault}"], indirect=True)

def read_fields(dut, line, timeout):
    match = dut.expect(line, timeout=timeout)

    fields = {}
    for pair in match.group(1).decode().split():
        key, value = pair.split("=", 1)
        fields[key] = value

    return fields

def expect_health_problem(dut, problem, timeout):
    match = dut.expect(HEALTH_LINE, timeout=timeout)
    problems = int(match.group(3), 16)
    print(f"health problems 0x{problems:02x}")
    assert problems & problem
    dut.expect_exact(HEALTH_FAILED, timeout=CHECK_TIMEOUT_S)
    return match

def expect_no_new_recording(dut, timeout):
    match = dut.expect(RECORDING_OR_DONE, timeout=timeout)
    assert match.group(0) == FAULT_DONE.encode()

def check_finished_capture(dut):
    dut.expect_exact(RECORDING_STOPPED, timeout=CAPTURE_TIMEOUT_S)

    summary = dut.expect(CAPTURE_SUMMARY, timeout=CHECK_TIMEOUT_S)
    peak_occupancy = int(summary.group(1))
    overflows = int(summary.group(2))
    print(f"peak occupancy {peak_occupancy} samples")
    assert overflows == 0

    dut.expect_exact(FINALIZED, timeout=CHECK_TIMEOUT_S)

    segment_frames = []
    fields = read_fields(dut, CHECK_LINE, CHECK_TIMEOUT_S)
    while "result" not in fields:
        assert fields["status"] == "ok", f"segment {fields['segment']} failed its check"
        segment_frames.append(int(fields["frames"]))
        fields = read_fields(dut, CHECK_LINE, CHECK_TIMEOUT_S)

    assert fields["result"] == "PASS"

    muted_frames = int(fields["muted_frames"])
    print(f"muted frames {muted_frames}")
    assert abs(muted_frames - EXPECTED_MUTED_FRAMES) <= MUTED_FRAME_TOLERANCE

    print(f"segment frames {segment_frames}")
    for frames in segment_frames[:-1]:
        assert frames == FULL_SEGMENT_FRAMES
    assert 0 < segment_frames[-1] <= FULL_SEGMENT_FRAMES
    assert abs(sum(segment_frames) - EXPECTED_FRAMES) <= FRAME_TOLERANCE

    num_uploaded = 0
    fields = read_fields(dut, UPLOAD_LINE, UPLOAD_TIMEOUT_S)
    while "result" not in fields:
        assert fields["status"] == "ok", f"segment {fields['segment']} failed to upload"
        num_uploaded += 1
        fields = read_fields(dut, UPLOAD_LINE, UPLOAD_TIMEOUT_S)

    assert fields["result"] == "PASS"
    assert num_uploaded == len(segment_frames)

    uploaded_while_recording = int(fields["while_recording"])
    print(f"uploaded while recording {uploaded_while_recording}")
    assert uploaded_while_recording > 0

def test_capture_session(dut: Dut):
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(RECORDING_STARTED, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(HEALTH_OK, timeout=CAPTURE_TIMEOUT_S)
    check_finished_capture(dut)

@pytest.mark.manual_setup
def test_fault_without_card(dut: Dut):
    dut.expect_exact(FAULT, timeout=BOOT_TIMEOUT_S)

@pytest.mark.fault
@fault_build("wifi_outage")
def test_wifi_down_mid_session(dut: Dut):
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(RECORDING_STARTED, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(WIFI_DOWN, timeout=CAPTURE_TIMEOUT_S)
    dut.expect_exact(HEALTH_OK, timeout=CAPTURE_TIMEOUT_S)
    dut.expect_exact(WIFI_UP, timeout=CAPTURE_TIMEOUT_S)
    check_finished_capture(dut)

@pytest.mark.fault
@fault_build("wifi_down_at_start")
def test_wifi_down_at_start(dut: Dut):
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(WIFI_DOWN, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(RECORDING_STARTED, timeout=CAPTURE_TIMEOUT_S)
    dut.expect_exact(HEALTH_OK, timeout=CAPTURE_TIMEOUT_S)
    dut.expect_exact(WIFI_UP, timeout=CAPTURE_TIMEOUT_S)
    dut.expect_exact(RECORDING_STOPPED, timeout=CAPTURE_TIMEOUT_S)
    dut.expect_exact(FINALIZED, timeout=CHECK_TIMEOUT_S)
    dut.expect_exact(FAULT_DONE, timeout=CHECK_TIMEOUT_S)

@pytest.mark.fault
@fault_build("card_full")
def test_card_full_refuses_to_record(dut: Dut):
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(CARD_FULL_REFUSED, timeout=BOOT_TIMEOUT_S)
    expect_no_new_recording(dut, CHECK_TIMEOUT_S)

@pytest.mark.fault
@fault_build("sd_write_glitch")
def test_sd_write_glitch_is_retried(dut: Dut):
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(RECORDING_STARTED, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact("segment write failed, retry 1 of 3", timeout=CAPTURE_TIMEOUT_S)
    expect_health_problem(dut, PROBLEM_SD_WRITE_ERRORS, CAPTURE_TIMEOUT_S)
    dut.expect_exact(RECORDING_STOPPED, timeout=CAPTURE_TIMEOUT_S)
    dut.expect_exact(FINALIZED, timeout=CHECK_TIMEOUT_S)
    dut.expect_exact(FAULT_DONE, timeout=CHECK_TIMEOUT_S)

@pytest.mark.fault
@fault_build("sd_write_error")
def test_sd_write_error_stops_and_finalizes(dut: Dut):
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(RECORDING_STARTED, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact("segment write failed after 3 retries", timeout=CAPTURE_TIMEOUT_S)
    dut.expect_exact(RECORDING_FAILED, timeout=CHECK_TIMEOUT_S)
    dut.expect_exact(FINALIZE_FAILED, timeout=CHECK_TIMEOUT_S)
    expect_no_new_recording(dut, CHECK_TIMEOUT_S)

@pytest.mark.fault
@fault_build("mic_silent")
def test_mic_silent_is_logged_and_reported(dut: Dut):
    capture_timeout_s = 120
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(RECORDING_STARTED, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact("mic silent for 30 s", timeout=capture_timeout_s)
    expect_health_problem(dut, PROBLEM_MIC_SILENT, capture_timeout_s)
    dut.expect_exact(RECORDING_STOPPED, timeout=capture_timeout_s)
    dut.expect_exact(FINALIZED, timeout=CHECK_TIMEOUT_S)
    dut.expect_exact(FAULT_DONE, timeout=CHECK_TIMEOUT_S)

@pytest.mark.fault
@fault_build("encoder_error")
def test_encoder_error_drops_a_frame(dut: Dut):
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(RECORDING_STARTED, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact("encoder failed (ESP_FAIL), frame dropped", timeout=CAPTURE_TIMEOUT_S)
    expect_health_problem(dut, PROBLEM_AUDIO_DROPPED, CAPTURE_TIMEOUT_S)
    dut.expect_exact(RECORDING_STOPPED, timeout=CAPTURE_TIMEOUT_S)
    dut.expect_exact(FINALIZED, timeout=CHECK_TIMEOUT_S)
    dut.expect_exact(FAULT_DONE, timeout=CHECK_TIMEOUT_S)

@pytest.mark.fault
@fault_build("encoder_stall")
def test_encoder_stall_counts_overflows(dut: Dut):
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(RECORDING_STARTED, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact("recorder fell behind", timeout=CAPTURE_TIMEOUT_S)
    expect_health_problem(dut, PROBLEM_BUFFER_OVERFLOW, CAPTURE_TIMEOUT_S)
    dut.expect_exact(RECORDING_STOPPED, timeout=CAPTURE_TIMEOUT_S)

    summary = dut.expect(CAPTURE_SUMMARY, timeout=CHECK_TIMEOUT_S)
    overflows = int(summary.group(2))
    print(f"overflows {overflows}")
    assert overflows > 0

    dut.expect_exact(FINALIZED, timeout=CHECK_TIMEOUT_S)
    dut.expect_exact(FAULT_DONE, timeout=CHECK_TIMEOUT_S)

@pytest.mark.fault
@fault_build("upload_failing")
def test_upload_failing_keeps_recording(dut: Dut):
    capture_timeout_s = 400
    finalize_timeout_s = 300
    dut.expect_exact(BOOT_DONE, timeout=BOOT_TIMEOUT_S)
    dut.expect_exact(RECORDING_STARTED, timeout=BOOT_TIMEOUT_S)

    health = expect_health_problem(dut, PROBLEM_UPLOAD_BACKLOG, capture_timeout_s)
    queue_depth = int(health.group(1))
    print(f"upload queue {queue_depth}")
    assert queue_depth >= MAX_UPLOAD_QUEUE_DEPTH
    assert health.group(2) == b"up"

    dut.expect_exact(RECORDING_STOPPED, timeout=capture_timeout_s)
    dut.expect_exact(FINALIZE_FAILED, timeout=finalize_timeout_s)
    dut.expect_exact(FAULT_DONE, timeout=CHECK_TIMEOUT_S)
