#!/usr/bin/env python3
"""Set up the rclone remotes the PC pipeline uses (design doc 9.1, 9.2).

The device's key can only write. The PC gets its own key that can only list
and read, scoped to the same bucket, created once with a key that can manage
keys:

    b2 key create --bucket <bucket> nocturne-pc listBuckets,listFiles,readFiles

Then:

    python tools/b2_remote.py --key-id <applicationKeyId>

Night records go to a second bucket, through a key that can also write but
never delete:

    b2 key create --bucket <bucket>-derived nocturne-pc-derived listBuckets,listFiles,readFiles,writeFiles
    python tools/b2_remote.py --derived --key-id <applicationKeyId>

The key itself comes from NOCTURNE_B2_READ_KEY (NOCTURNE_B2_DERIVED_KEY with
--derived), or is prompted for, so it never lands in shell history. The tool
authorizes with B2 and refuses a key that can delete, that can write unless
--derived, that covers anything but exactly one bucket, or that is limited to
a name prefix. It then creates the rclone remote (default `nocturne`, or
`nocturne-derived`) and lists the folders through it to prove it works.
--check-only stops after the key check; --replace updates an existing remote.
"""

import argparse
import base64
import getpass
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

AUTHORIZE_URL = "https://api.backblazeb2.com/b2api/v4/b2_authorize_account"
AUTHORIZE_TIMEOUT_S = 15


class Profile:
    def __init__(self, name, refusal, default_remote, key_id_env, key_env, required, allowed_extra):
        self.name = name
        self.refusal = refusal
        self.default_remote = default_remote
        self.key_id_env = key_id_env
        self.key_env = key_env
        self.required = required
        self.allowed_extra = allowed_extra


# rclone resolves the bucket ID through b2_list_buckets, so it needs listBuckets
# as well as the two capabilities that list and fetch files.
READ_PROFILE = Profile(
    name="read-only",
    refusal="the key is not read-only",
    default_remote="nocturne",
    key_id_env="NOCTURNE_B2_READ_KEY_ID",
    key_env="NOCTURNE_B2_READ_KEY",
    required=("listBuckets", "listFiles", "readFiles"),
    allowed_extra=(),
)

DERIVED_PROFILE = Profile(
    name="derived",
    refusal="the key can do more than read and write files",
    default_remote="nocturne-derived",
    key_id_env="NOCTURNE_B2_DERIVED_KEY_ID",
    key_env="NOCTURNE_B2_DERIVED_KEY",
    required=("listBuckets", "listFiles", "readFiles", "writeFiles"),
    allowed_extra=("writeFiles",),
)

READ_ONLY_PREFIXES = ("list", "read")


class KeyCheck:
    def __init__(self):
        self.bucket_name = None
        self.capabilities = []
        self.problems = []


def authorize(key_id, key):
    credentials = f"{key_id}:{key}".encode()
    header = "Basic " + base64.b64encode(credentials).decode()
    request = urllib.request.Request(AUTHORIZE_URL, headers={"Authorization": header})

    try:
        with urllib.request.urlopen(request, timeout=AUTHORIZE_TIMEOUT_S) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        raise RuntimeError(f"B2 refused the key: HTTP {error.code} {describe_error_body(error.read())}") from None
    except urllib.error.URLError as error:
        raise RuntimeError(f"could not reach B2: {error.reason}") from None


def describe_error_body(body):
    try:
        message = json.loads(body).get("message")
    except ValueError:
        message = None

    if message:
        return message

    return body.decode("utf-8", errors="replace").strip()


def is_allowed(capability, profile):
    for prefix in READ_ONLY_PREFIXES:
        if capability.startswith(prefix):
            return True

    return capability in profile.allowed_extra


def check_key(response, profile=READ_PROFILE):
    """Check a v4 b2_authorize_account response against a profile's capabilities and one bucket."""
    check = KeyCheck()

    try:
        allowed = response["apiInfo"]["storageApi"]["allowed"]
    except (KeyError, TypeError):
        check.problems.append("the response has no apiInfo.storageApi.allowed (not a v4 response?)")
        return check

    capabilities = allowed.get("capabilities")
    if capabilities is None:
        capabilities = []
    check.capabilities = list(capabilities)

    for capability in profile.required:
        if capability not in capabilities:
            check.problems.append(f"the key lacks {capability}")

    for capability in capabilities:
        if not is_allowed(capability, profile):
            check.problems.append(f"{profile.refusal}: it has {capability}")

    buckets = allowed.get("buckets")
    if not buckets:
        check.problems.append("the key is not restricted to a bucket")
    elif len(buckets) > 1:
        check.problems.append(f"the key covers {len(buckets)} buckets, not one")
    else:
        check.bucket_name = buckets[0].get("name")
        if not check.bucket_name:
            check.problems.append("the key's bucket has no name")

    name_prefix = allowed.get("namePrefix")
    if name_prefix:
        check.problems.append(f"the key only sees names starting with {name_prefix!r}")

    return check


def remote_path(remote, bucket_name):
    return f"{remote}:{bucket_name}"


def parse_session_listing(text):
    """Session folder names from `rclone lsf --dirs-only`, which ends each with /."""
    sessions = []

    for line in text.splitlines():
        name = line.strip().rstrip("/")
        if name:
            sessions.append(name)

    sessions.sort()
    return sessions


def run_rclone(arguments):
    try:
        return subprocess.run(["rclone"] + arguments, capture_output=True, text=True)
    except FileNotFoundError:
        sys.exit("FAIL: rclone is not on PATH; install it from https://rclone.org/downloads/")


def remote_exists(remote):
    result = run_rclone(["listremotes"])
    if result.returncode != 0:
        sys.exit(f"FAIL: rclone listremotes: {result.stderr.strip()}")

    return f"{remote}:" in result.stdout.split()


def configure_remote(remote, key_id, key, replace):
    if remote_exists(remote):
        if not replace:
            sys.exit(f"FAIL: rclone already has a remote named {remote}; pass --replace to update it")
        command = "update"
        arguments = ["config", "update", remote]
    else:
        command = "create"
        arguments = ["config", "create", remote, "b2"]

    arguments += [f"account={key_id}", f"key={key}", "--non-interactive"]

    # rclone echoes the remote's settings, key included, so its output is kept
    # off the terminal and only the error is shown.
    result = run_rclone(arguments)
    if result.returncode != 0:
        sys.exit(f"FAIL: rclone config {command}: {result.stderr.strip()}")

    return command


def list_sessions(path):
    result = run_rclone(["lsf", path, "--dirs-only"])
    if result.returncode != 0:
        sys.exit(f"FAIL: rclone lsf {path}: {result.stderr.strip()}")

    return parse_session_listing(result.stdout)


def read_key_id(args, profile):
    if args.key_id:
        return args.key_id

    key_id = os.environ.get(profile.key_id_env)
    if key_id:
        return key_id

    sys.exit(f"FAIL: pass --key-id or set {profile.key_id_env}")


def read_key(profile):
    key = os.environ.get(profile.key_env)
    if key:
        return key

    return getpass.getpass(f"B2 application key ({profile.name}): ")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--key-id", help="the key's applicationKeyId, or set NOCTURNE_B2_READ_KEY_ID")
    parser.add_argument("--derived", action="store_true", help="set up the derived bucket's remote, which can write")
    parser.add_argument("--remote", help="name of the rclone remote to create")
    parser.add_argument("--replace", action="store_true", help="update the remote if it already exists")
    parser.add_argument("--check-only", action="store_true", help="check the key, leave rclone alone")
    args = parser.parse_args()

    profile = READ_PROFILE
    if args.derived:
        profile = DERIVED_PROFILE

    remote = args.remote
    if not remote:
        remote = profile.default_remote

    key_id = read_key_id(args, profile)
    key = read_key(profile)

    try:
        response = authorize(key_id, key)
    except RuntimeError as error:
        sys.exit(f"FAIL: {error}")

    check = check_key(response, profile)
    print(f"capabilities: {', '.join(check.capabilities)}")

    if check.problems:
        for problem in check.problems:
            print(f"FAIL: {problem}")
        sys.exit(1)

    print(f"bucket:       {check.bucket_name}")

    if args.check_only:
        print("PASS")
        return

    command = configure_remote(remote, key_id, key, args.replace)
    path = remote_path(remote, check.bucket_name)
    print(f"remote:       {path} ({command}d)")

    sessions = list_sessions(path)
    print(f"sessions:     {len(sessions)}")

    print("PASS")


if __name__ == "__main__":
    main()
