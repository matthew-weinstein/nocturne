#!/usr/bin/env python3
"""Decrypt Nocturne segment files chunk by chunk (design doc 4.4).

A segment is an 8-byte NCT2 header followed by AES-256-GCM chunk records. The
reader walks the records in order, opens each under the AAD implied by the
segment index and the chunk's position, and stops at the first record that
runs past EOF or fails its tag. Everything before that point is kept.

    python tools/decrypt_segment.py --salt-mac 44b176a468c8 seg_0003.opus.enc
    python tools/decrypt_segment.py --salt-mac 44b176a468c8 sessions/20260906_2312 --out-dir plain/

The passphrase comes from --passphrase or the NOCTURNE_PASSPHRASE environment
variable. The MAC is the one key_manager logs when it derives the device key.

A torn or failing final chunk is what a power cut leaves behind and is reported
as a note. A failing chunk with more records after it is corruption, and the
exit status is 1.
"""

import argparse
import os
import re
import sys
from pathlib import Path

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

from decrypt_chunk import (
    KEY_BYTES,
    LENGTH_FIELD_BYTES,
    NONCE_BYTES,
    RECORD_OVERHEAD_BYTES,
    build_aad,
    derive_key,
)

SEGMENT_MAGIC = b"NCT2"
SEGMENT_FORMAT_VERSION = 1
SEGMENT_HEADER_BYTES = 8
FRAME_LENGTH_BYTES = 2
FRAME_MS = 20
SEGMENT_NAME = re.compile(r"seg_(\d{4})\.opus\.enc$")


class SegmentResult:
    def __init__(self, path, segment_index):
        self.path = path
        self.segment_index = segment_index
        self.file_bytes = 0
        self.frames_per_chunk = 0
        self.plaintext = bytearray()
        self.packets = []
        self.num_chunks = 0
        self.stop_reason: str | None = None
        self.discarded_bytes = 0
        self.errors = []
        self.warnings = []


def split_packets(plaintext):
    """Parse a chunk's plaintext into its length-prefixed Opus packets."""
    packets = []
    offset = 0

    while offset < len(plaintext):
        if offset + FRAME_LENGTH_BYTES > len(plaintext):
            raise ValueError(f"frame length field cut off at byte {offset}")
        num_bytes = int.from_bytes(plaintext[offset:offset + FRAME_LENGTH_BYTES], "little")
        offset += FRAME_LENGTH_BYTES
        if num_bytes == 0 or offset + num_bytes > len(plaintext):
            raise ValueError(f"frame at byte {offset - FRAME_LENGTH_BYTES} declares {num_bytes} bytes")
        packets.append(plaintext[offset:offset + num_bytes])
        offset += num_bytes

    return packets


def read_header(data, result):
    if len(data) < SEGMENT_HEADER_BYTES:
        result.errors.append(f"file is {len(data)} bytes, shorter than the {SEGMENT_HEADER_BYTES}-byte header")
        return False
    if data[:4] != SEGMENT_MAGIC:
        result.errors.append(f"bad magic {data[:4]!r}, expected {SEGMENT_MAGIC!r}")
        return False
    if data[4] != SEGMENT_FORMAT_VERSION:
        result.errors.append(f"format version {data[4]}, expected {SEGMENT_FORMAT_VERSION}")
        return False

    result.frames_per_chunk = data[5]
    return True


def walk_segment(key, path, segment_index):
    data = Path(path).read_bytes()
    result = SegmentResult(Path(path), segment_index)
    result.file_bytes = len(data)

    if not read_header(data, result):
        return result

    aead = AESGCM(key)
    offset = SEGMENT_HEADER_BYTES
    short_chunk_index = None

    while offset < len(data):
        remaining = len(data) - offset
        chunk_index = result.num_chunks

        if remaining < RECORD_OVERHEAD_BYTES:
            result.stop_reason = f"chunk {chunk_index} torn: {remaining} bytes left, shorter than a record"
            result.discarded_bytes = remaining
            break

        num_bytes = int.from_bytes(data[offset:offset + LENGTH_FIELD_BYTES], "little")
        record_bytes = num_bytes + RECORD_OVERHEAD_BYTES

        if num_bytes == 0 or record_bytes > remaining:
            result.stop_reason = (f"chunk {chunk_index} torn: declares {num_bytes} ciphertext bytes, "
                                  f"{remaining - RECORD_OVERHEAD_BYTES} present")
            result.discarded_bytes = remaining
            break

        nonce_start = offset + LENGTH_FIELD_BYTES
        nonce = data[nonce_start:nonce_start + NONCE_BYTES]
        sealed = data[nonce_start + NONCE_BYTES:offset + record_bytes]

        try:
            plaintext = aead.decrypt(nonce, sealed, build_aad(segment_index, chunk_index))
        except InvalidTag:
            result.stop_reason = f"chunk {chunk_index} failed its tag"
            result.discarded_bytes = remaining
            if record_bytes < remaining:
                result.errors.append(f"chunk {chunk_index} failed its tag with {remaining - record_bytes} bytes after it")
            break

        try:
            packets = split_packets(plaintext)
        except ValueError as error:
            result.errors.append(f"chunk {chunk_index} decrypted but does not parse: {error}")
            break

        if short_chunk_index is not None:
            result.warnings.append(f"chunk {short_chunk_index} held fewer than {result.frames_per_chunk} frames but was not last")
            short_chunk_index = None
        if len(packets) > result.frames_per_chunk:
            result.warnings.append(f"chunk {chunk_index} holds {len(packets)} frames, header says {result.frames_per_chunk}")
        elif len(packets) < result.frames_per_chunk:
            short_chunk_index = chunk_index

        result.plaintext += plaintext
        result.packets += packets
        result.num_chunks += 1
        offset += record_bytes

    return result


def segment_index_from_name(path):
    match = SEGMENT_NAME.search(Path(path).name)
    return int(match.group(1)) if match else None


def collect_segments(inputs):
    segments = []

    for item in inputs:
        path = Path(item)
        if path.is_dir():
            segments += sorted(p for p in path.iterdir() if SEGMENT_NAME.search(p.name))
        else:
            segments.append(path)

    return segments


def report(result):
    seconds = len(result.packets) * FRAME_MS / 1000
    print(f"{result.path.name}: segment {result.segment_index}, {result.file_bytes} bytes on disk")

    if result.frames_per_chunk:
        print(f"  chunks:  {result.num_chunks} of up to {result.frames_per_chunk} frames")
        print(f"  frames:  {len(result.packets)} ({seconds:.2f} s), {len(result.plaintext)} plaintext bytes")
    if result.stop_reason:
        print(f"  stopped: {result.stop_reason}, {result.discarded_bytes} bytes discarded")
    for warning in result.warnings:
        print(f"  warning: {warning}")
    for error in result.errors:
        print(f"  FAIL:    {error}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="segment files or session directories")
    parser.add_argument("--segment", type=int, help="segment index in the AAD, instead of the one in the filename")
    parser.add_argument("--key", help="32-byte key in hex, instead of deriving one")
    parser.add_argument("--passphrase", default=os.environ.get("NOCTURNE_PASSPHRASE"), help="passphrase to derive the key from")
    parser.add_argument("--salt-mac", help="6-byte device MAC in hex, the start of the salt")
    parser.add_argument("--out-dir", help="write each segment's plaintext here as seg_NNNN.packets")
    args = parser.parse_args()

    if args.key:
        key = bytes.fromhex(args.key)
    elif args.passphrase and args.salt_mac:
        key = derive_key(args.passphrase, bytes.fromhex(args.salt_mac))
    else:
        sys.exit("FAIL: give --key, or --salt-mac with --passphrase or NOCTURNE_PASSPHRASE")

    if len(key) != KEY_BYTES:
        sys.exit(f"FAIL: key is {len(key)} bytes, expected {KEY_BYTES}")

    segments = collect_segments(args.inputs)
    if not segments:
        sys.exit("FAIL: no segment files found")
    if args.segment is not None and len(segments) > 1:
        sys.exit("FAIL: --segment applies to a single file")

    if args.out_dir:
        os.makedirs(args.out_dir, exist_ok=True)

    failed = False

    for path in segments:
        segment_index = args.segment if args.segment is not None else segment_index_from_name(path)
        if segment_index is None:
            sys.exit(f"FAIL: cannot read a segment index from {path.name}; pass --segment")

        result = walk_segment(key, path, segment_index)
        report(result)
        failed |= bool(result.errors)

        if args.out_dir and result.plaintext:
            out_path = Path(args.out_dir) / f"seg_{segment_index:04d}.packets"
            out_path.write_bytes(result.plaintext)
            print(f"  wrote:   {out_path}")

    if failed:
        sys.exit(1)

    print("PASS")


if __name__ == "__main__":
    main()
