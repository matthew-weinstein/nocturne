#!/usr/bin/env python3
"""Decrypt one Nocturne chunk record and check the round trip (design doc 4.2).

The device self-test seals a known chunk, writes it to /sdcard/test_chunk.bin
and prints it as hex between two markers. Feed either one to this script:

    python tools/decrypt_chunk.py test_chunk.bin
    python tools/decrypt_chunk.py serial.log --hex

It derives the key exactly as the device does, decrypts with the chunk's AAD,
compares the plaintext against the pattern the self-test sealed, and confirms
that the wrong segment or chunk index fails the tag.
"""

import argparse
import re
import sys

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC

KEY_BYTES = 32
NONCE_BYTES = 12
TAG_BYTES = 16
LENGTH_FIELD_BYTES = 4
RECORD_OVERHEAD_BYTES = LENGTH_FIELD_BYTES + NONCE_BYTES + TAG_BYTES

PBKDF2_ITERATIONS = 100000
SALT_SUFFIX = b"nocturne"

# The known-answer vector in main/test/self_test.c.
TEST_PASSPHRASE = "nocturne-test-passphrase"
TEST_SALT_MAC = "001122334455"
TEST_SEGMENT_INDEX = 42
TEST_CHUNK_INDEX = 7
TEST_CHUNK_FRAMES = 50
TEST_CHUNK_FRAME_BYTES = 46

HEX_BEGIN = "---- nocturne chunk record begin ----"
HEX_END = "---- nocturne chunk record end ----"


def derive_key(passphrase, mac):
    """PBKDF2-HMAC-SHA256 over the 14-byte salt: MAC || "nocturne"."""
    kdf = PBKDF2HMAC(
        algorithm=hashes.SHA256(),
        length=KEY_BYTES,
        salt=mac + SALT_SUFFIX,
        iterations=PBKDF2_ITERATIONS,
    )
    return kdf.derive(passphrase.encode())


def build_aad(segment_index, chunk_index):
    return segment_index.to_bytes(2, "little") + chunk_index.to_bytes(4, "little")


def parse_record(data):
    """Split a chunk record into nonce, ciphertext+tag and the bytes consumed."""
    if len(data) < RECORD_OVERHEAD_BYTES:
        raise ValueError(f"record is {len(data)} bytes, shorter than the {RECORD_OVERHEAD_BYTES}-byte overhead")

    num_bytes = int.from_bytes(data[:LENGTH_FIELD_BYTES], "little")
    record_bytes = num_bytes + RECORD_OVERHEAD_BYTES

    if num_bytes == 0:
        raise ValueError("record declares a zero-length chunk")
    if record_bytes > len(data):
        raise ValueError(f"record declares {num_bytes} ciphertext bytes but only {len(data) - RECORD_OVERHEAD_BYTES} are present (torn tail)")

    nonce = data[LENGTH_FIELD_BYTES:LENGTH_FIELD_BYTES + NONCE_BYTES]
    sealed = data[LENGTH_FIELD_BYTES + NONCE_BYTES:record_bytes]
    return nonce, sealed, num_bytes, record_bytes


def open_chunk(key, segment_index, chunk_index, data):
    nonce, sealed, num_bytes, record_bytes = parse_record(data)
    plaintext = AESGCM(key).decrypt(nonce, sealed, build_aad(segment_index, chunk_index))

    if len(plaintext) != num_bytes:
        raise ValueError(f"decrypted {len(plaintext)} bytes, record declared {num_bytes}")

    return plaintext, record_bytes


def build_test_chunk():
    """The plaintext main/test/self_test.c seals: 50 length-prefixed frames."""
    out = bytearray()

    for frame in range(TEST_CHUNK_FRAMES):
        out += TEST_CHUNK_FRAME_BYTES.to_bytes(2, "little")
        out += bytes((frame * 31 + i * 7 + 11) & 0xFF for i in range(TEST_CHUNK_FRAME_BYTES))

    return bytes(out)


def read_hex(text):
    if HEX_BEGIN in text and HEX_END in text:
        text = text.split(HEX_BEGIN, 1)[1].split(HEX_END, 1)[0]

    lines = [line.strip() for line in text.splitlines()]
    hex_lines = [line for line in lines if len(line) >= 2 and len(line) % 2 == 0 and re.fullmatch(r"[0-9a-fA-F]+", line)]

    if not hex_lines:
        raise ValueError("no hex lines found in the input")

    return bytes.fromhex("".join(hex_lines))


def load_input(path, is_hex):
    raw = sys.stdin.buffer.read() if path == "-" else open(path, "rb").read()
    return read_hex(raw.decode("utf-8", errors="replace")) if is_hex else raw


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("record", help="chunk record file, or - for stdin")
    parser.add_argument("--hex", action="store_true", help="the input is serial log text, not raw bytes")
    parser.add_argument("--segment", type=int, default=TEST_SEGMENT_INDEX, help="segment index in the AAD")
    parser.add_argument("--chunk", type=int, default=TEST_CHUNK_INDEX, help="chunk index in the AAD")
    parser.add_argument("--key", help="32-byte key in hex, instead of deriving one")
    parser.add_argument("--passphrase", default=TEST_PASSPHRASE, help="passphrase to derive the key from")
    parser.add_argument("--salt-mac", default=TEST_SALT_MAC, help="6-byte device MAC in hex, the start of the salt")
    parser.add_argument("--out", help="write the plaintext here")
    parser.add_argument("--no-check", action="store_true", help="skip the comparison against the self-test plaintext")
    parser.add_argument("--show-key", action="store_true", help="print the whole key, not just a fingerprint")
    args = parser.parse_args()

    data = load_input(args.record, args.hex)
    key = bytes.fromhex(args.key) if args.key else derive_key(args.passphrase, bytes.fromhex(args.salt_mac))

    if len(key) != KEY_BYTES:
        sys.exit(f"FAIL: key is {len(key)} bytes, expected {KEY_BYTES}")

    print(f"record:    {len(data)} bytes")
    print(f"key:       {key.hex() if args.show_key else key.hex()[:8] + '...'}")

    try:
        plaintext, record_bytes = open_chunk(key, args.segment, args.chunk, data)
    except (InvalidTag, ValueError) as error:
        sys.exit(f"FAIL: {'tag verification failed' if isinstance(error, InvalidTag) else error}")

    print(f"plaintext: {record_bytes - RECORD_OVERHEAD_BYTES} bytes, segment {args.segment} chunk {args.chunk}")

    if record_bytes < len(data):
        print(f"note:      {len(data) - record_bytes} trailing bytes were not part of this record")

    failures = []

    if not args.no_check:
        expected = build_test_chunk()
        if plaintext == expected:
            print(f"round trip: byte-identical to the {len(expected)}-byte self-test chunk")
        else:
            failures.append(f"plaintext does not match the self-test chunk ({len(plaintext)} bytes decrypted, {len(expected)} expected)")

    for label, segment_index, chunk_index in (
        ("segment", args.segment + 1, args.chunk),
        ("chunk", args.segment, args.chunk + 1),
    ):
        try:
            open_chunk(key, segment_index, chunk_index, data)
            failures.append(f"the wrong {label} index still decrypted, so the AAD does not bind it")
        except InvalidTag:
            print(f"aad:       the wrong {label} index fails the tag")

    if args.out:
        with open(args.out, "wb") as file:
            file.write(plaintext)
        print(f"wrote:     {args.out}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        sys.exit(1)

    print("PASS")


if __name__ == "__main__":
    main()
