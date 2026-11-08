"""The Nocturne passphrase, kept in the OS credential store.

    cd tools
    python -m nocturne_pc.passphrase
    python -m nocturne_pc.passphrase --forget

The first stores the passphrase once, asked for twice and never echoed. The
pipeline then reads it from NOCTURNE_PASSPHRASE if set, else from the
credential store through keyring (Windows Credential Manager, macOS
Keychain or Linux Secret Service, unlocked by the login, so a scheduled job
running as the same user can read it), else asks.
"""

import argparse
import os
import sys
from getpass import getpass

PASSPHRASE_ENV = "NOCTURNE_PASSPHRASE"
KEYRING_SERVICE = "nocturne"
KEYRING_USERNAME = "passphrase"

def load_keyring():
    try:
        import keyring
    except ImportError:
        return None
    return keyring

def stored_passphrase():
    keyring = load_keyring()
    if keyring is None:
        return None

    return keyring.get_password(KEYRING_SERVICE, KEYRING_USERNAME)

def read_passphrase():
    passphrase = os.environ.get(PASSPHRASE_ENV)
    if passphrase:
        return passphrase

    passphrase = stored_passphrase()
    if passphrase:
        return passphrase

    return getpass("Nocturne passphrase: ")

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--forget", action="store_true", help="remove the stored passphrase")
    args = parser.parse_args()

    keyring = load_keyring()
    if keyring is None:
        sys.exit("FAIL: keyring is not installed; pip install -r requirements.txt")

    if args.forget:
        if stored_passphrase() is None:
            print("no passphrase stored")
            return
        keyring.delete_password(KEYRING_SERVICE, KEYRING_USERNAME)
        print("passphrase removed")
        return

    passphrase = getpass("Nocturne passphrase: ")
    if not passphrase:
        sys.exit("FAIL: empty passphrase")
    if getpass("again: ") != passphrase:
        sys.exit("FAIL: the two entries differ")

    keyring.set_password(KEYRING_SERVICE, KEYRING_USERNAME, passphrase)
    print(f"stored in {keyring.get_keyring().__class__.__name__}")

if __name__ == "__main__":
    main()
