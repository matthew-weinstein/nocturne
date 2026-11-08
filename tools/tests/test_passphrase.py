import sys
import types

import pytest

from nocturne_pc import passphrase

def fake_keyring(stored):
    module = types.ModuleType("keyring")
    module.get_password = lambda service, username: stored.get((service, username))
    return module

@pytest.fixture
def prompt(monkeypatch):
    asked = []

    def answer(text):
        asked.append(text)
        return "typed"

    monkeypatch.setattr(passphrase, "getpass", answer)
    return asked

def test_the_environment_comes_first(monkeypatch, prompt):
    monkeypatch.setenv("NOCTURNE_PASSPHRASE", "from env")
    monkeypatch.setitem(sys.modules, "keyring", fake_keyring({("nocturne", "passphrase"): "stored"}))

    assert passphrase.read_passphrase() == "from env"
    assert prompt == []

def test_the_credential_store_is_used_without_the_environment(monkeypatch, prompt):
    monkeypatch.delenv("NOCTURNE_PASSPHRASE", raising=False)
    monkeypatch.setitem(sys.modules, "keyring", fake_keyring({("nocturne", "passphrase"): "stored"}))

    assert passphrase.read_passphrase() == "stored"
    assert prompt == []

def test_nothing_stored_asks(monkeypatch, prompt):
    monkeypatch.delenv("NOCTURNE_PASSPHRASE", raising=False)
    monkeypatch.setitem(sys.modules, "keyring", fake_keyring({}))

    assert passphrase.read_passphrase() == "typed"
    assert len(prompt) == 1

def test_without_keyring_installed_it_asks(monkeypatch, prompt):
    monkeypatch.delenv("NOCTURNE_PASSPHRASE", raising=False)
    monkeypatch.setitem(sys.modules, "keyring", None)

    assert passphrase.read_passphrase() == "typed"
    assert len(prompt) == 1
