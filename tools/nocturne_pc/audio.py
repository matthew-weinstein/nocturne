"""Decrypt and decode a session's audio on demand (design doc 9.5).

    from nocturne_pc.audio import SessionAudio
    audio = SessionAudio.open("20261107_1754")
    samples = audio.decode(1, first_frame=250, num_frames=500)

Sessions stay encrypted in audio/; nothing decoded is written to disk. A
range is decoded with up to 25 frames before it (0.5 s of preroll) and one
after it, then cut to exactly its frames, so a slice matches the same
stretch of a whole-session decode. The device's CELT frames settle in 8;
SILK, which VOIP or a lower bitrate would bring, needs about 25.

A range runs on into later segments in index order. Across a resume that
joins the audio either side of the outage; the clock anchors (§22) say where
the gap was. The key is derived from the session's device_id and the
passphrase in NOCTURNE_PASSPHRASE, or a prompt.
"""

import functools
import io
import json
import os
import re
from getpass import getpass
from pathlib import Path

import numpy as np
import soundfile

from decrypt_chunk import derive_key
from decrypt_segment import SEGMENT_NAME, walk_segment
from export_audio import write_ogg_opus_stream
from nocturne_pc import data_root

SAMPLE_RATE_HZ = 16000
FRAME_SAMPLES = 320
PREROLL_FRAMES = 25
PASSPHRASE_ENV = "NOCTURNE_PASSPHRASE"
CLIP_NAME = re.compile(r"seg_(\d{4})_c(\d{6})\.opus\.enc$")

def read_passphrase():
    passphrase = os.environ.get(PASSPHRASE_ENV)
    if passphrase:
        return passphrase

    return getpass("Nocturne passphrase: ")

@functools.cache
def device_key(device_id, passphrase):
    return derive_key(passphrase, bytes.fromhex(device_id))

def decode_packets(packets, num_frames):
    """The first num_frames frames of packets as 16 kHz float32.

    The Ogg stream's pre-skip drops the encoder's 6.5 ms lookahead, so sample
    0 lines up with the first packet's audio. The stream's end is trimmed by
    the same amount; a packet past num_frames covers that, and without one
    the shortfall is zero-filled.
    """
    stream = io.BytesIO()
    write_ogg_opus_stream(stream, packets)
    stream.seek(0)

    decoded, sample_rate = soundfile.read(stream, dtype="float32")
    if sample_rate != SAMPLE_RATE_HZ:
        raise ValueError(f"decoded at {sample_rate} Hz, expected {SAMPLE_RATE_HZ}")

    num_samples = num_frames * FRAME_SAMPLES
    samples = np.zeros(num_samples, dtype=np.float32)
    num_copied = min(num_samples, len(decoded))
    samples[:num_copied] = decoded[:num_copied]
    return samples

def open_segment(key, path, segment_index, first_chunk=0):
    result = walk_segment(key, path, segment_index, first_chunk)
    if result.errors:
        raise ValueError(f"{path.name}: {'; '.join(result.errors)}")

    return result

def decode_clip(path, key):
    """A clip (design doc 9.13), decoded whole. It starts cold, without preroll."""
    path = Path(path)
    match = CLIP_NAME.search(path.name)
    if match is None:
        raise ValueError(f"{path.name} is not named seg_NNNN_cNNNNNN.opus.enc")

    segment_index = int(match.group(1))
    first_chunk = int(match.group(2))
    result = open_segment(key, path, segment_index, first_chunk)
    if result.stop_reason:
        raise ValueError(f"{path.name}: {result.stop_reason}; a clip holds only whole chunks")

    return decode_packets(result.packets, len(result.packets))

class SessionAudio:
    def __init__(self, session_dir, key):
        self.session_dir = Path(session_dir)
        self.key = key
        self.segment_paths = {}
        self.segment_packets = {}

        for path in sorted(self.session_dir.iterdir()):
            match = SEGMENT_NAME.search(path.name)
            if match:
                self.segment_paths[int(match.group(1))] = path

    @classmethod
    def open(cls, session_id, passphrase=None):
        session_dir = data_root() / "audio" / session_id
        metadata = json.loads((session_dir / "session.json").read_text(encoding="utf-8"))

        if passphrase is None:
            passphrase = read_passphrase()

        return cls(session_dir, device_key(metadata["device_id"], passphrase))

    def segments(self):
        return sorted(self.segment_paths)

    def packets(self, segment):
        if segment not in self.segment_paths:
            raise ValueError(f"segment {segment} is not in {self.session_dir}")

        if segment not in self.segment_packets:
            result = open_segment(self.key, self.segment_paths[segment], segment)
            self.segment_packets[segment] = result.packets

        return self.segment_packets[segment]

    def num_frames(self, segment):
        return len(self.packets(segment))

    def packets_from(self, segment, first_frame, count):
        """Up to count packets from (segment, first_frame) on, crossing into later segments."""
        collected = []
        offset = first_frame

        while len(collected) < count and segment in self.segment_paths:
            remaining = count - len(collected)
            collected += self.packets(segment)[offset:offset + remaining]
            segment += 1
            offset = 0

        return collected

    def preroll(self, segment, first_frame):
        before = self.packets(segment)[:first_frame]
        if len(before) < PREROLL_FRAMES and segment - 1 in self.segment_paths:
            before = self.packets(segment - 1) + before

        return before[-PREROLL_FRAMES:]

    def decode(self, segment, first_frame=0, num_frames=None):
        """Frames from (segment, first_frame) as 16 kHz float32; by default to the segment's end."""
        segment_frames = self.num_frames(segment)
        if first_frame < 0 or first_frame > segment_frames:
            raise ValueError(f"frame {first_frame} is outside segment {segment}'s {segment_frames} frames")

        if num_frames is None:
            num_frames = segment_frames - first_frame

        wanted = self.packets_from(segment, first_frame, num_frames + 1)
        if len(wanted) < num_frames:
            raise ValueError(f"{num_frames} frames from segment {segment} frame {first_frame} "
                             f"run past the session's audio ({len(wanted)} there)")

        preroll = self.preroll(segment, first_frame)
        samples = decode_packets(preroll + wanted, len(preroll) + num_frames)
        return samples[len(preroll) * FRAME_SAMPLES:]
