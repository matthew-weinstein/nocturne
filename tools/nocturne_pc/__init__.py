"""Nocturne's PC pipeline."""

import os
from pathlib import Path

DATA_ENV = "NOCTURNE_DATA"


def data_root():
    root = os.environ.get(DATA_ENV)
    if root:
        return Path(root)

    return Path.home() / "nocturne-data"
