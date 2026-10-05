import struct
from dataclasses import dataclass

from export_audio import PRE_SKIP_SAMPLES, SAMPLE_RATE_HZ, ogg_crc, packet_samples, write_ogg_opus
from segment_builder import make_packet

CELT_20MS_SAMPLES = 960
SILK_20MS_SAMPLES = 960

TOC_CONFIG_SHIFT = 3
TOC_CONFIG_SILK_NB_20MS = 1
TOC_CONFIG_CELT_FB_20MS = 31
TOC_CODE_ONE_FRAME = 0
TOC_CODE_TWO_FRAMES = 1
TOC_CODE_ARBITRARY_FRAMES = 3

OGG_CAPTURE = b"OggS"
OGG_HEADER_BYTES = 27
OGG_FIELDS_OFFSET = 5
OGG_FIELDS_FORMAT = "<BqIIIB"
OGG_CRC_OFFSET = 22
FLAG_BEGIN = 0x02
FLAG_END = 0x04

OPUS_HEAD_PRE_SKIP_OFFSET = 10
OPUS_HEAD_SAMPLE_RATE_OFFSET = 12

@dataclass
class Page:
    flags: int
    granule: int
    sequence: int
    lacing: bytes
    body: bytes

def toc(config, code):
    return (config << TOC_CONFIG_SHIFT) | code

def read_page(data, offset):
    """Parse the Ogg page at offset, check its CRC, and return it with the offset of the next page."""
    assert data[offset:offset + len(OGG_CAPTURE)] == OGG_CAPTURE

    flags, granule, _serial, sequence, crc, num_lacing = struct.unpack_from(
        OGG_FIELDS_FORMAT, data, offset + OGG_FIELDS_OFFSET)

    lacing_start = offset + OGG_HEADER_BYTES
    body_start = lacing_start + num_lacing
    lacing = data[lacing_start:body_start]
    end = body_start + sum(lacing)

    page_with_zero_crc = bytearray(data[offset:end])
    struct.pack_into("<I", page_with_zero_crc, OGG_CRC_OFFSET, 0)
    assert ogg_crc(page_with_zero_crc) == crc

    page = Page(flags, granule, sequence, bytes(lacing), data[body_start:end])
    return page, end

def read_pages(data):
    pages = []
    offset = 0

    while offset < len(data):
        page, offset = read_page(data, offset)
        pages.append(page)

    return pages

def test_packet_samples_from_the_toc_byte():
    one_celt_frame = bytes([toc(TOC_CONFIG_CELT_FB_20MS, TOC_CODE_ONE_FRAME)])
    one_silk_frame = bytes([toc(TOC_CONFIG_SILK_NB_20MS, TOC_CODE_ONE_FRAME)])
    two_celt_frames = bytes([toc(TOC_CONFIG_CELT_FB_20MS, TOC_CODE_TWO_FRAMES)])
    four_celt_frames = bytes([toc(TOC_CONFIG_CELT_FB_20MS, TOC_CODE_ARBITRARY_FRAMES), 4])

    assert packet_samples(one_celt_frame) == CELT_20MS_SAMPLES
    assert packet_samples(one_silk_frame) == SILK_20MS_SAMPLES
    assert packet_samples(two_celt_frames) == 2 * CELT_20MS_SAMPLES
    assert packet_samples(four_celt_frames) == 4 * CELT_20MS_SAMPLES

def test_ogg_stream_has_valid_pages_and_headers(tmp_path):
    num_packets = 120
    packets = []
    for frame in range(num_packets):
        packets.append(make_packet(frame))

    path = tmp_path / "out.opus"
    granule = write_ogg_opus(path, packets)
    pages = read_pages(path.read_bytes())

    assert granule == num_packets * CELT_20MS_SAMPLES
    for expected_sequence, page in enumerate(pages):
        assert page.sequence == expected_sequence

    head_page, tags_page, last_page = pages[0], pages[1], pages[-1]

    assert head_page.flags == FLAG_BEGIN
    assert head_page.body.startswith(b"OpusHead")
    pre_skip = struct.unpack_from("<H", head_page.body, OPUS_HEAD_PRE_SKIP_OFFSET)[0]
    sample_rate = struct.unpack_from("<I", head_page.body, OPUS_HEAD_SAMPLE_RATE_OFFSET)[0]
    assert pre_skip == PRE_SKIP_SAMPLES
    assert sample_rate == SAMPLE_RATE_HZ

    assert tags_page.body.startswith(b"OpusTags")
    assert last_page.flags == FLAG_END
    assert last_page.granule == granule

def test_packets_of_255_bytes_or_more_use_continued_lacing(tmp_path):
    path = tmp_path / "out.opus"
    write_ogg_opus(path, [make_packet(0, num_bytes=300)])

    first_audio_page = read_pages(path.read_bytes())[2]
    assert first_audio_page.lacing == bytes([255, 45])
