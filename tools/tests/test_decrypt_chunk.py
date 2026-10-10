import pytest
from cryptography.exceptions import InvalidTag

from decrypt_chunk import (
    HEX_BEGIN,
    HEX_END,
    LENGTH_FIELD_BYTES,
    RECORD_OVERHEAD_BYTES,
    TEST_CHUNK_INDEX,
    TEST_SEGMENT_INDEX,
    open_chunk,
    parse_record,
    read_hex,
)
from segment_builder import seal_record

DEVICE_KAT_KEY = "8720ffc87977b5ad1ac6f5312e4d24100c2f5066b8d9a31a454a31d0cd331b36"

# Sealed once by the PC tools. The crypto host test opens the same bytes with
# aes_gcm_open_chunk, so the device and the PC are checked against one record.
PC_SEALED_RECORD = bytes.fromhex(
    "18000000a0a1a2a3a4a5a6a7a8a9aaabaeaf6eb06e65fa401500ec7001a0adb9"
    "3799cb314f741f5f06fb946a64c19c7e156096b4138c38e5"
)
PC_SEALED_PLAINTEXT_BYTES = 24

def pc_sealed_plaintext():
    plaintext = bytearray()

    for i in range(PC_SEALED_PLAINTEXT_BYTES):
        plaintext.append((i * 7 + 11) & 0xFF)

    return bytes(plaintext)

def test_key_derivation_matches_the_device_vector(kat_key):
    assert kat_key.hex() == DEVICE_KAT_KEY

def test_pc_sealed_record_matches_the_host_test_vector(kat_key):
    plaintext, consumed = open_chunk(kat_key, TEST_SEGMENT_INDEX, TEST_CHUNK_INDEX, PC_SEALED_RECORD)

    assert plaintext == pc_sealed_plaintext()
    assert consumed == len(PC_SEALED_RECORD)

@pytest.mark.parametrize("segment_index, chunk_index", [
    (TEST_SEGMENT_INDEX + 1, TEST_CHUNK_INDEX),
    (TEST_SEGMENT_INDEX, TEST_CHUNK_INDEX + 1),
])
def test_aad_binds_segment_and_chunk(kat_key, segment_index, chunk_index):
    with pytest.raises(InvalidTag):
        open_chunk(kat_key, segment_index, chunk_index, PC_SEALED_RECORD)

def test_parse_record_rejects_short_empty_and_torn_records(kat_key):
    record = seal_record(kat_key, segment_index=0, chunk_index=0, plaintext=b"payload")

    shorter_than_overhead = record[:RECORD_OVERHEAD_BYTES - 1]
    with pytest.raises(ValueError, match="shorter than"):
        parse_record(shorter_than_overhead)

    zero_length = bytes(LENGTH_FIELD_BYTES) + record[LENGTH_FIELD_BYTES:]
    with pytest.raises(ValueError, match="zero-length"):
        parse_record(zero_length)

    missing_last_byte = record[:-1]
    with pytest.raises(ValueError, match="torn tail"):
        parse_record(missing_last_byte)

def test_read_hex_takes_only_the_lines_between_markers():
    log = f"""I (123) crypto_test: sealing
deadbeef
{HEX_BEGIN}
00112233
4455
{HEX_END}
cafe
"""
    assert read_hex(log) == bytes.fromhex("001122334455")

def test_read_hex_without_any_hex_fails():
    with pytest.raises(ValueError):
        read_hex("no hex here")
