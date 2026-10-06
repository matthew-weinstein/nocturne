import re

from pytest_embedded import Dut

from decrypt_chunk import (
    HEX_BEGIN,
    HEX_END,
    TEST_CHUNK_INDEX,
    TEST_PASSPHRASE,
    TEST_SALT_MAC,
    TEST_SEGMENT_INDEX,
    build_test_chunk,
    derive_key,
    open_chunk,
    read_hex,
)

CASE_TIMEOUT_S = 120
MENU_PROMPT = "Press ENTER to see the list of tests"
CHUNK_CASE = "a chunk sealed on the AES accelerator opens on the PC"
ONE_CASE_PASSED = "1 Tests 0 Failures"

RECORD_BEGIN = re.escape(HEX_BEGIN).encode()
RECORD_END = re.escape(HEX_END).encode()
RECORD_PATTERN = re.compile(RECORD_BEGIN + rb".*?" + RECORD_END, re.DOTALL)

def test_every_automatic_case(dut: Dut):
    dut.run_all_single_board_cases(group="!manual", reset=True, timeout=CASE_TIMEOUT_S)

def test_chunk_sealed_on_the_device_opens_on_the_pc(dut: Dut):
    dut.expect_exact(MENU_PROMPT)
    dut.write(f'"{CHUNK_CASE}"')

    match = dut.expect(RECORD_PATTERN, timeout=CASE_TIMEOUT_S)
    dut.expect_exact(ONE_CASE_PASSED)

    record = read_hex(match.group(0).decode())
    key = derive_key(TEST_PASSPHRASE, bytes.fromhex(TEST_SALT_MAC))
    plaintext, consumed = open_chunk(key, TEST_SEGMENT_INDEX, TEST_CHUNK_INDEX, record)

    assert plaintext == build_test_chunk()
    assert consumed == len(record)
