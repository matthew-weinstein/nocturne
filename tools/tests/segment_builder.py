"""Builds Nocturne segment files in memory, in the layout the device writes (design doc 10)."""

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

from decrypt_chunk import LENGTH_FIELD_BYTES, NONCE_BYTES, build_aad

SEGMENT_MAGIC = b"NCT2"
SEGMENT_FORMAT_VERSION = 1
FRAMES_PER_CHUNK = 50
FRAME_LENGTH_BYTES = 2

OPUS_CELT_20MS_TOC = 0xF8
DEFAULT_PACKET_BYTES = 40

def segment_header():
    reserved = bytes(2)
    return SEGMENT_MAGIC + bytes([SEGMENT_FORMAT_VERSION, FRAMES_PER_CHUNK]) + reserved

def make_packet(frame, num_bytes=DEFAULT_PACKET_BYTES):
    """An Opus-shaped packet: a CELT 20 ms TOC byte, then a pattern unique to the frame."""
    packet = bytearray([OPUS_CELT_20MS_TOC])

    for i in range(num_bytes - 1):
        packet.append((frame * 31 + i * 7) & 0xFF)

    return bytes(packet)

def frame_packets(packets):
    """A chunk's plaintext: each packet prefixed with its 2-byte little-endian length."""
    plaintext = bytearray()

    for packet in packets:
        plaintext += len(packet).to_bytes(FRAME_LENGTH_BYTES, "little")
        plaintext += packet

    return bytes(plaintext)

def seal_record(key, segment_index, chunk_index, plaintext):
    """One chunk record: length, nonce, ciphertext and tag. The nonce is fixed so tests are repeatable."""
    nonce = bytes([chunk_index]) * NONCE_BYTES
    aad = build_aad(segment_index, chunk_index)
    sealed = AESGCM(key).encrypt(nonce, plaintext, aad)

    length = len(plaintext).to_bytes(LENGTH_FIELD_BYTES, "little")
    return length + nonce + sealed

def make_segment(key, segment_index, frames_per_chunk):
    """A whole segment file: the header, then one sealed record per entry in frames_per_chunk."""
    data = bytearray(segment_header())
    next_frame = 0

    for chunk_index, num_frames in enumerate(frames_per_chunk):
        packets = []
        for frame in range(next_frame, next_frame + num_frames):
            packets.append(make_packet(frame))
        next_frame += num_frames

        plaintext = frame_packets(packets)
        data += seal_record(key, segment_index, chunk_index, plaintext)

    return bytes(data)
