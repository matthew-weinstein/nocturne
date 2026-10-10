from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]

@pytest.fixture
def app_path():
    return str(REPO_ROOT)
