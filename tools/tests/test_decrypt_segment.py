import pytest

from decrypt_segment import collect_segments, segment_index_from_name, split_packets, walk_segment
from segment_builder import FRAMES_PER_CHUNK, make_segment

SEGMENT_INDEX = 3
FIRST_CIPHERTEXT_OFFSET = 24

def walk(tmp_path, key, data):
    path = tmp_path / f"seg_{SEGMENT_INDEX:04d}.opus.enc"
    path.write_bytes(data)
    return walk_segment(key, path, SEGMENT_INDEX)

def flip_bit(data, offset):
    flipped = bytearray(data)
    flipped[offset] ^= 0x01
    return bytes(flipped)

def test_complete_segment_with_a_short_final_chunk(tmp_path, kat_key):
    data = make_segment(kat_key, SEGMENT_INDEX, [50, 50, 20])
    result = walk(tmp_path, kat_key, data)

    assert result.num_chunks == 3
    assert len(result.packets) == 120
    assert result.frames_per_chunk == FRAMES_PER_CHUNK
    assert result.stop_reason is None
    assert result.errors == []
    assert result.warnings == []

def test_torn_final_chunk_is_discarded_without_error(tmp_path, kat_key):
    data = make_segment(kat_key, SEGMENT_INDEX, [50, 50, 50])
    torn = data[:-10]
    result = walk(tmp_path, kat_key, torn)

    assert result.num_chunks == 2
    assert len(result.packets) == 100
    assert "torn" in result.stop_reason
    assert result.errors == []

def test_tail_shorter_than_a_record_is_discarded(tmp_path, kat_key):
    stray_bytes = bytes([1, 2, 3])
    data = make_segment(kat_key, SEGMENT_INDEX, [50]) + stray_bytes
    result = walk(tmp_path, kat_key, data)

    assert result.num_chunks == 1
    assert result.discarded_bytes == len(stray_bytes)
    assert result.errors == []

def test_final_chunk_failing_its_tag_is_discarded(tmp_path, kat_key):
    data = make_segment(kat_key, SEGMENT_INDEX, [50, 50])
    last_tag_byte = len(data) - 1
    result = walk(tmp_path, kat_key, flip_bit(data, last_tag_byte))

    assert result.num_chunks == 1
    assert "failed its tag" in result.stop_reason
    assert result.errors == []

def test_failing_tag_with_records_after_it_is_corruption(tmp_path, kat_key):
    data = make_segment(kat_key, SEGMENT_INDEX, [50, 50])
    result = walk(tmp_path, kat_key, flip_bit(data, FIRST_CIPHERTEXT_OFFSET))

    assert result.num_chunks == 0
    assert len(result.errors) == 1

def test_bad_magic_is_rejected(tmp_path, kat_key):
    data = make_segment(kat_key, SEGMENT_INDEX, [50])
    old_magic = b"NCT1" + data[4:]
    result = walk(tmp_path, kat_key, old_magic)

    assert result.num_chunks == 0
    assert len(result.errors) == 1
    assert "magic" in result.errors[0]

def test_short_chunk_that_is_not_last_is_a_warning(tmp_path, kat_key):
    data = make_segment(kat_key, SEGMENT_INDEX, [50, 20, 50])
    result = walk(tmp_path, kat_key, data)

    assert result.num_chunks == 3
    assert result.errors == []
    assert len(result.warnings) == 1
    assert "chunk 1" in result.warnings[0]

def test_split_packets_reads_length_prefixed_frames():
    plaintext = bytes([2, 0]) + b"ab" + bytes([1, 0]) + b"c"
    assert split_packets(plaintext) == [b"ab", b"c"]

@pytest.mark.parametrize("plaintext", [
    bytes([0, 0]),
    bytes([5, 0]) + b"ab",
    bytes([2]),
])
def test_split_packets_rejects_bad_lengths(plaintext):
    with pytest.raises(ValueError):
        split_packets(plaintext)

def test_segment_index_comes_from_the_filename(tmp_path):
    assert segment_index_from_name("seg_0042.opus.enc") == 42
    assert segment_index_from_name(tmp_path / "manifest.bin") is None

def test_session_directory_lists_segments_in_order(tmp_path):
    for name in ["seg_0002.opus.enc", "manifest.bin", "seg_0000.opus.enc", "seg_0001.opus.enc"]:
        (tmp_path / name).write_bytes(b"")

    names = []
    for path in collect_segments([tmp_path]):
        names.append(path.name)

    assert names == ["seg_0000.opus.enc", "seg_0001.opus.enc", "seg_0002.opus.enc"]
