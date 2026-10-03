#!/usr/bin/env python3
"""Decrypt Nocturne segments and export them as playable audio.

The segments are decrypted exactly as decrypt_segment.py does, and their Opus
packets are joined in index order into one Ogg Opus stream. No decoder is
needed for that step, so the .opus file plays anywhere Opus does (VLC, a
browser). --wav also decodes it to 16 kHz mono PCM through soundfile.

    python tools/export_audio.py --salt-mac 44b176a468c8 sessions/20260906_2312 --out night.opus
    python tools/export_audio.py --salt-mac 44b176a468c8 sessions/20260906_2312 --out night.opus --wav

Segments are joined end to end. A resumed session has real gaps where the
device was down, and the export does not mark them.
"""

import argparse
import struct
import sys
from pathlib import Path

from decrypt_segment import (
    add_key_arguments,
    collect_segments,
    key_from_args,
    report,
    segment_index_from_name,
    walk_segment,
)

SAMPLE_RATE_HZ = 16000
CHANNELS = 1
GRANULE_RATE_HZ = 48000
PRE_SKIP_SAMPLES = 312
PACKETS_PER_PAGE = 50
STREAM_SERIAL = 0x4E435432

FLAG_BEGIN = 0x02
FLAG_END = 0x04

FRAME_SAMPLES_SILK = (480, 960, 1920, 2880)
FRAME_SAMPLES_HYBRID = (480, 960)
FRAME_SAMPLES_CELT = (120, 240, 480, 960)


def make_crc_table():
    table = []
    for byte in range(256):
        crc = byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) if crc & 0x80000000 else crc << 1
        table.append(crc & 0xFFFFFFFF)
    return table


CRC_TABLE = make_crc_table()


def ogg_crc(data):
    crc = 0
    for byte in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ CRC_TABLE[(crc >> 24) ^ byte]
    return crc


def packet_samples(packet):
    """Samples at 48 kHz in one Opus packet, from its TOC byte (RFC 6716 3.1)."""
    config = packet[0] >> 3
    if config < 12:
        frame_samples = FRAME_SAMPLES_SILK[config % 4]
    elif config < 16:
        frame_samples = FRAME_SAMPLES_HYBRID[config % 2]
    else:
        frame_samples = FRAME_SAMPLES_CELT[config % 4]

    code = packet[0] & 0x03
    if code == 0:
        num_frames = 1
    elif code in (1, 2):
        num_frames = 2
    else:
        num_frames = packet[1] & 0x3F

    return frame_samples * num_frames


class OggWriter:
    def __init__(self, file):
        self.file = file
        self.sequence = 0

    def write_page(self, packets, granule, flags):
        lacing = bytearray()
        for packet in packets:
            lacing += b"\xff" * (len(packet) // 255)
            lacing.append(len(packet) % 255)
        if len(lacing) > 255:
            raise ValueError(f"page needs {len(lacing)} lacing values, the limit is 255")

        header = struct.pack("<4sBBqIIIB", b"OggS", 0, flags, granule, STREAM_SERIAL,
                             self.sequence, 0, len(lacing))
        page = bytearray(header + lacing + b"".join(packets))
        struct.pack_into("<I", page, 22, ogg_crc(page))

        self.file.write(page)
        self.sequence += 1


def opus_head():
    return struct.pack("<8sBBHIhB", b"OpusHead", 1, CHANNELS, PRE_SKIP_SAMPLES, SAMPLE_RATE_HZ, 0, 0)


def opus_tags():
    vendor = b"nocturne"
    return struct.pack("<8sI", b"OpusTags", len(vendor)) + vendor + struct.pack("<I", 0)


def write_ogg_opus(path, packets):
    with open(path, "wb") as file:
        writer = OggWriter(file)
        writer.write_page([opus_head()], 0, FLAG_BEGIN)
        writer.write_page([opus_tags()], 0, 0)

        granule = 0
        for start in range(0, len(packets), PACKETS_PER_PAGE):
            page_packets = packets[start:start + PACKETS_PER_PAGE]
            granule += sum(packet_samples(packet) for packet in page_packets)
            is_last = start + PACKETS_PER_PAGE >= len(packets)
            writer.write_page(page_packets, granule, FLAG_END if is_last else 0)

    return granule


def write_wav(opus_path, wav_path):
    import soundfile

    audio, sample_rate = soundfile.read(opus_path, dtype="int16")
    soundfile.write(wav_path, audio, sample_rate, subtype="PCM_16")
    return len(audio), sample_rate


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="segment files or session directories")
    add_key_arguments(parser)
    parser.add_argument("--out", required=True, help="Ogg Opus file to write")
    parser.add_argument("--wav", action="store_true", help="also decode to a WAV file beside it")
    args = parser.parse_args()

    key = key_from_args(args)

    segments = collect_segments(args.inputs)
    if not segments:
        sys.exit("FAIL: no segment files found")

    indexed = []
    for path in segments:
        segment_index = segment_index_from_name(path)
        if segment_index is None:
            sys.exit(f"FAIL: cannot read a segment index from {path.name}")
        indexed.append((segment_index, path))

    packets = []
    failed = False

    for segment_index, path in sorted(indexed):
        result = walk_segment(key, path, segment_index)
        report(result)
        failed |= bool(result.errors)
        packets += result.packets

    if failed:
        sys.exit("FAIL: a segment did not decrypt cleanly; nothing written")
    if not packets:
        sys.exit("FAIL: no audio in the given segments")

    out_path = Path(args.out)
    granule = write_ogg_opus(out_path, packets)
    seconds = (granule - PRE_SKIP_SAMPLES) / GRANULE_RATE_HZ
    print(f"wrote {out_path}: {len(packets)} packets, {seconds:.2f} s")

    if args.wav:
        wav_path = out_path.with_suffix(".wav")
        num_samples, sample_rate = write_wav(out_path, wav_path)
        print(f"wrote {wav_path}: {num_samples} samples at {sample_rate} Hz, {num_samples / sample_rate:.2f} s")


if __name__ == "__main__":
    main()
