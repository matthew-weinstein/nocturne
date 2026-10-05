import pytest

from decrypt_chunk import TEST_PASSPHRASE, TEST_SALT_MAC, derive_key

@pytest.fixture(scope="session")
def kat_key():
    mac = bytes.fromhex(TEST_SALT_MAC)
    return derive_key(TEST_PASSPHRASE, mac)
