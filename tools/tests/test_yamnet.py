import numpy as np
import pytest

from model.make_feature_vectors import vector_samples
from nocturne_pc import yamnet
from nocturne_pc.features import log_mel_patch
from nocturne_pc.yamnet import (
    CLASS_GROUPS,
    FIRST_LAYER,
    NUM_CLASSES,
    OTHER_GROUP,
    SEPARABLE_LAYERS,
    Yamnet,
    class_names,
    fold_batchnorm,
    group_members,
    group_scores,
    spatial_conv,
    weights_path,
)

TENSORFLOW_TOP_SCORES = (
    (132, 0.987223),
    (154, 0.443917),
    (153, 0.430136),
    (237, 0.396904),
    (147, 0.289686),
    (234, 0.257505),
    (133, 0.134710),
    (158, 0.008636),
)

def naive_conv(images, kernel, stride):
    """Direct 3x3 "same" convolution, one output pixel at a time, odd padding after."""
    batch, height, width, _ = images.shape
    out_height = -(-height // stride)
    out_width = -(-width // stride)
    pad_rows = max((out_height - 1) * stride + 3 - height, 0)
    pad_columns = max((out_width - 1) * stride + 3 - width, 0)
    top = pad_rows // 2
    left = pad_columns // 2

    output = np.zeros((batch, out_height, out_width, kernel.shape[-1]))
    for row in range(out_height):
        for column in range(out_width):
            for kernel_row in range(3):
                for kernel_column in range(3):
                    source_row = row * stride + kernel_row - top
                    source_column = column * stride + kernel_column - left
                    if 0 <= source_row < height and 0 <= source_column < width:
                        output[:, row, column] += images[:, source_row, source_column] * kernel[kernel_row, kernel_column]
    return output

def random_weights(rng):
    weights = {}

    def batchnorm(prefix, channels):
        weights[f"{prefix}/bn/beta"] = rng.standard_normal(channels).astype(np.float32) * 0.1
        weights[f"{prefix}/bn/moving_mean"] = rng.standard_normal(channels).astype(np.float32) * 0.1
        weights[f"{prefix}/bn/moving_variance"] = rng.uniform(0.5, 2.0, channels).astype(np.float32)

    name, _, filters = FIRST_LAYER
    weights[f"{name}/conv/kernel"] = rng.standard_normal((3, 3, 1, filters)).astype(np.float32) * 0.3
    batchnorm(f"{name}/conv", filters)
    channels = filters

    for name, _, filters in SEPARABLE_LAYERS:
        weights[f"{name}/depthwise_conv/depthwise_kernel"] = (
            rng.standard_normal((3, 3, channels, 1)).astype(np.float32) * 0.3)
        batchnorm(f"{name}/depthwise_conv", channels)
        weights[f"{name}/pointwise_conv/kernel"] = (
            rng.standard_normal((1, 1, channels, filters)).astype(np.float32) / np.sqrt(channels))
        batchnorm(f"{name}/pointwise_conv", filters)
        channels = filters

    weights["logits/kernel"] = rng.standard_normal((channels, NUM_CLASSES)).astype(np.float32) * 0.05
    weights["logits/bias"] = np.zeros(NUM_CLASSES, dtype=np.float32)
    return weights

def test_every_group_member_is_a_yamnet_class_and_other_takes_the_rest():
    names = class_names()
    members = group_members(names)

    assert len(names) == NUM_CLASSES
    assert list(members) == list(CLASS_GROUPS) + [OTHER_GROUP]

    claimed = []
    for indices in members.values():
        claimed += list(indices)
    assert sorted(claimed) == list(range(NUM_CLASSES))

def test_a_misspelt_member_is_refused():
    names = class_names()
    names[names.index("Snoring")] = "Snore"

    with pytest.raises(ValueError, match="Snoring"):
        group_members(names)

def test_a_group_scores_its_highest_member():
    names = class_names()
    members = group_members(names)
    scores = np.zeros((2, NUM_CLASSES), dtype=np.float32)
    scores[0, names.index("Throat clearing")] = 0.7
    scores[0, names.index("Cough")] = 0.4
    scores[1, names.index("Cough")] = 0.9

    grouped = group_scores(scores, members)
    cough = list(members).index("cough")

    assert grouped[:, cough].tolist() == pytest.approx([0.7, 0.9])

@pytest.mark.parametrize("stride", [1, 2])
@pytest.mark.parametrize("size", [(6, 4), (5, 7)])
def test_the_convolution_matches_a_direct_one(stride, size):
    rng = np.random.default_rng(1)
    images = rng.standard_normal((2, *size, 3)).astype(np.float32)
    kernel = rng.standard_normal((3, 3, 3)).astype(np.float32)
    bias = np.zeros(3, dtype=np.float32)

    expected = np.maximum(naive_conv(images, kernel, stride), 0.0)

    np.testing.assert_allclose(spatial_conv(images, kernel, bias, stride), expected, atol=1e-5)

def test_folded_batch_norm_equals_batch_norm_after_the_kernel():
    rng = np.random.default_rng(2)
    weights = random_weights(rng)
    kernel = weights["layer2/pointwise_conv/kernel"][0, 0]
    inputs = rng.standard_normal((5, kernel.shape[0])).astype(np.float32)

    folded, bias = fold_batchnorm(kernel, weights, "layer2/pointwise_conv")
    mean = weights["layer2/pointwise_conv/bn/moving_mean"]
    variance = weights["layer2/pointwise_conv/bn/moving_variance"]
    beta = weights["layer2/pointwise_conv/bn/beta"]
    expected = (inputs @ kernel - mean) / np.sqrt(variance + 1e-4) + beta

    np.testing.assert_allclose(inputs @ folded + bias, expected, atol=1e-5)

def test_scores_are_one_sigmoid_per_class_per_patch():
    model = Yamnet(random_weights(np.random.default_rng(3)))
    patches = np.random.default_rng(4).standard_normal((3, 96, 64)).astype(np.float32)

    scores = model.scores(patches)

    assert scores.shape == (3, NUM_CLASSES)
    assert np.all((scores > 0.0) & (scores < 1.0))
    np.testing.assert_allclose(model.scores(patches[1:2])[0], scores[1], atol=1e-6)

def test_a_patch_of_the_wrong_shape_is_refused():
    model = Yamnet(random_weights(np.random.default_rng(5)))

    with pytest.raises(ValueError, match="96, 64"):
        model.scores(np.zeros((1, 64, 96)))

def test_weights_failing_their_hash_are_not_kept(tmp_path, monkeypatch):
    def fake_download(url, destination):
        destination.write_bytes(b"not yamnet")

    monkeypatch.setattr(yamnet.urllib.request, "urlretrieve", fake_download)
    path = tmp_path / "models" / "yamnet.h5"

    with pytest.raises(ValueError, match="SHA-256"):
        yamnet.fetch_weights(path)

    assert list(path.parent.iterdir()) == []

def test_the_real_weights_match_tensorflow():
    pytest.importorskip("h5py")
    if not weights_path().exists():
        pytest.skip(f"no YAMNet weights at {weights_path()}; the teacher fetches them on first run")

    model = Yamnet.load()
    patch = log_mel_patch(vector_samples())[np.newaxis]
    scores = model.scores(patch)[0]

    for index, expected in TENSORFLOW_TOP_SCORES:
        assert scores[index] == pytest.approx(expected, abs=1e-5)
