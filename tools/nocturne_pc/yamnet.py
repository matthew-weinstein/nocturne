"""YAMNet's core in numpy: log-mel patches in, 521 class scores out.

    from nocturne_pc.yamnet import Yamnet
    model = Yamnet.load()
    scores = model.scores(patches)        # (N, 96, 64) -> (N, 521)

The network is MobileNetV1 as tensorflow/models research/audioset/yamnet
defines it: a 3x3 convolution, thirteen depthwise-separable blocks, average
pooling and a sigmoid layer. Batch norm is folded into each convolution at
load. The weights are Google's yamnet.h5, fetched once into models/ at the
data root and checked against a pinned SHA-256; yamnet_class_map.csv beside
this file names the classes.
"""

import csv
import hashlib
import urllib.request
from pathlib import Path

import numpy as np

from nocturne_pc import data_root

WEIGHTS_URL = "https://storage.googleapis.com/audioset/yamnet.h5"
WEIGHTS_SHA256 = "13c3308955bbfaef262f175ac9c40e47b134573a93984f009220dd7cc12a1744"
WEIGHTS_NAME = "yamnet.h5"
CLASS_MAP_PATH = Path(__file__).with_name("yamnet_class_map.csv")

NUM_CLASSES = 521
KERNEL_SIZE = 3
BATCHNORM_EPSILON = 1e-4
PATCH_FRAMES = 96
PATCH_BANDS = 64

FIRST_LAYER = ("layer1", 2, 32)
SEPARABLE_LAYERS = (
    ("layer2", 1, 64),
    ("layer3", 2, 128),
    ("layer4", 1, 128),
    ("layer5", 2, 256),
    ("layer6", 1, 256),
    ("layer7", 2, 512),
    ("layer8", 1, 512),
    ("layer9", 1, 512),
    ("layer10", 1, 512),
    ("layer11", 1, 512),
    ("layer12", 1, 512),
    ("layer13", 2, 1024),
    ("layer14", 1, 1024),
)

CLASS_GROUPS = {
    "speech": ("Speech", "Conversation", "Narration, monologue", "Whispering", "Babbling"),
    "snoring": ("Snoring", "Snort"),
    "breathing": ("Breathing", "Pant", "Wheeze"),
    "gasp": ("Gasp",),
    "cough": ("Cough", "Throat clearing"),
    "sneeze": ("Sneeze", "Sniff"),
    "movement": ("Rustle", "Squeak", "Creak"),
    "footsteps": ("Walk, footsteps",),
    "door": ("Door", "Slam", "Knock", "Cupboard open or close"),
    "water": ("Toilet flush", "Water tap, faucet", "Sink (filling or washing)"),
    "alarm": ("Alarm clock", "Alarm", "Ringtone", "Telephone bell ringing", "Beep, bleep", "Buzzer"),
    "music": ("Music",),
    "traffic": ("Vehicle", "Car", "Truck", "Motorcycle", "Siren", "Emergency vehicle"),
    "dog": ("Dog", "Bark"),
    "weather": ("Rain", "Thunder", "Thunderstorm"),
    "bird": ("Bird", "Bird vocalization, bird call, bird song", "Chirp, tweet"),
    "silence": ("Silence",),
}
OTHER_GROUP = "other"

def class_names(path=CLASS_MAP_PATH):
    names = []
    with open(path, encoding="utf-8", newline="") as class_map:
        reader = csv.reader(class_map)
        next(reader)
        for _, _, display_name in reader:
            names.append(display_name)
    return names

def group_members(names):
    """Each group's class indices; "other" takes every class no group names."""
    index_of = {}
    for index, name in enumerate(names):
        index_of[name] = index

    members = {}
    claimed = set()
    for group, group_names in CLASS_GROUPS.items():
        indices = []
        for name in group_names:
            if name not in index_of:
                raise ValueError(f"group {group} names {name!r}, which YAMNet does not have")
            indices.append(index_of[name])
            claimed.add(index_of[name])
        members[group] = np.array(indices)

    unclaimed = []
    for index in range(len(names)):
        if index not in claimed:
            unclaimed.append(index)
    members[OTHER_GROUP] = np.array(unclaimed)
    return members

def group_scores(scores, members):
    """(N, len(members)) group scores, each the highest of its members' scores."""
    grouped = np.zeros((len(scores), len(members)), dtype=np.float32)
    for column, indices in enumerate(members.values()):
        grouped[:, column] = scores[:, indices].max(axis=1)
    return grouped

def weights_path():
    return data_root() / "models" / WEIGHTS_NAME

def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()

def fetch_weights(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    partial = path.with_suffix(".partial")
    print(f"fetching {WEIGHTS_URL} into {path}")
    urllib.request.urlretrieve(WEIGHTS_URL, partial)

    if file_sha256(partial) != WEIGHTS_SHA256:
        partial.unlink()
        raise ValueError(f"{WEIGHTS_URL} does not match the pinned SHA-256")
    partial.replace(path)

def read_weights(path):
    """Every weight in yamnet.h5 by its Keras name, e.g. layer2/depthwise_conv/depthwise_kernel."""
    import h5py

    weights = {}
    with h5py.File(path, "r") as source:
        for layer_name in source.attrs["layer_names"]:
            layer = source[layer_name]
            for weight_name in layer.attrs["weight_names"]:
                key = weight_name.decode() if isinstance(weight_name, bytes) else weight_name
                weights[key.removesuffix(":0")] = np.array(layer[weight_name], dtype=np.float32)
    return weights

def fold_batchnorm(kernel, weights, prefix):
    """A kernel whose last axis is the output channel, and its bias, with batch norm folded in."""
    scale = 1.0 / np.sqrt(weights[f"{prefix}/bn/moving_variance"] + BATCHNORM_EPSILON)
    bias = weights[f"{prefix}/bn/beta"] - weights[f"{prefix}/bn/moving_mean"] * scale
    return (kernel * scale).astype(np.float32), bias.astype(np.float32)

def pad_same(images, stride):
    """Zero padding as TensorFlow's "same" places it: any odd pixel goes after."""
    padding = [(0, 0)]
    for size in images.shape[1:3]:
        out_size = -(-size // stride)
        total = max((out_size - 1) * stride + KERNEL_SIZE - size, 0)
        padding.append((total // 2, total - total // 2))
    padding.append((0, 0))
    return np.pad(images, padding)

def spatial_conv(images, kernel, bias, stride):
    """3x3 convolution over (N, H, W, C) at a stride; kernel (3, 3, C or 1, C_out) per tap."""
    padded = pad_same(images, stride)
    out_height = -(-images.shape[1] // stride)
    out_width = -(-images.shape[2] // stride)
    output = np.zeros((images.shape[0], out_height, out_width, kernel.shape[-1]), dtype=np.float32)
    product = np.empty_like(output)

    for row in range(KERNEL_SIZE):
        for column in range(KERNEL_SIZE):
            taps = padded[:, row:row + stride * out_height:stride, column:column + stride * out_width:stride, :]
            np.multiply(taps, kernel[row, column], out=product)
            output += product

    output += bias
    return np.maximum(output, 0.0, out=output)

def pointwise_conv(images, kernel, bias):
    batch, height, width, channels = images.shape
    output = images.reshape(-1, channels) @ kernel
    output += bias
    np.maximum(output, 0.0, out=output)
    return output.reshape(batch, height, width, kernel.shape[-1])

class Yamnet:
    def __init__(self, weights):
        name, stride, _ = FIRST_LAYER
        kernel = weights[f"{name}/conv/kernel"][:, :, 0, :]
        self.first = (stride, *fold_batchnorm(kernel, weights, f"{name}/conv"))

        self.blocks = []
        for name, stride, _ in SEPARABLE_LAYERS:
            depthwise = weights[f"{name}/depthwise_conv/depthwise_kernel"][:, :, :, 0]
            pointwise = weights[f"{name}/pointwise_conv/kernel"][0, 0]
            self.blocks.append((stride,
                                fold_batchnorm(depthwise, weights, f"{name}/depthwise_conv"),
                                fold_batchnorm(pointwise, weights, f"{name}/pointwise_conv")))

        self.logits_kernel = weights["logits/kernel"]
        self.logits_bias = weights["logits/bias"]

    @classmethod
    def load(cls, path=None):
        if path is None:
            path = weights_path()
        path = Path(path)
        if not path.exists():
            fetch_weights(path)
        return cls(read_weights(path))

    def scores(self, patches):
        """(N, 96, 64) log-mel patches to (N, 521) sigmoid scores."""
        patches = np.asarray(patches, dtype=np.float32)
        if patches.shape[1:] != (PATCH_FRAMES, PATCH_BANDS):
            raise ValueError(f"patches must be (N, {PATCH_FRAMES}, {PATCH_BANDS}), not {patches.shape}")

        stride, kernel, bias = self.first
        images = spatial_conv(patches[..., np.newaxis], kernel, bias, stride)

        for stride, (depthwise, depthwise_bias), (pointwise, pointwise_bias) in self.blocks:
            images = spatial_conv(images, depthwise, depthwise_bias, stride)
            images = pointwise_conv(images, pointwise, pointwise_bias)

        embeddings = images.mean(axis=(1, 2))
        logits = embeddings @ self.logits_kernel + self.logits_bias
        return 1.0 / (1.0 + np.exp(-logits))
