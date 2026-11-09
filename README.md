# Nocturne

Firmware for a bedside sleep recorder, and the software on your computer
that turns its nights into an account of how you slept.

Each morning you find out how the night went: when you fell
asleep, how much you snored, whether you coughed, tossed and turned, or hit
snooze four times. Over weeks it starts to answer the more interesting
questions. Do I sleep worse on Sundays? When it rains? After a late night?

Everything stays yours. The audio is encrypted on the device and all the listening happens on your own computer.

## How it works

You press the button when you get into bed and hold it when you get up.
In between, the board records, encrypts and uploads the night in small
pieces, so a power cut or a dropped WiFi connection costs seconds rather
than the whole night.

Your computer pulls each night, listens through it with
[YAMNet](https://github.com/tensorflow/models/tree/master/research/audioset/yamnet),
and turns what it hears into a few numbers worth looking at, set next to
that night's weather and sunrise.

## The device

You'll need ESP-IDF 6.1 and a [Backblaze B2](https://www.backblaze.com/cloud-storage) bucket.

```sh
b2 key create --bucket <bucket> nocturne-device writeFiles
cp main/secrets.h.example main/secrets.h    # WiFi, that b2 key, a passphrase
idf.py build flash
```

Don't lose the passphrase. Without it, nothing can be decrypted.

## Your computer

You'll need Python 3.12 and [rclone](https://rclone.org/).

```sh
pip install -r tools/requirements.txt
b2 key create --bucket <bucket> nocturne-pc listBuckets,listFiles,readFiles
python tools/b2_remote.py --key-id <keyId>
cd tools && python -m nocturne_pc.passphrase    # saved to your keychain, asked once
```

Then put your coordinates, for the weather and sunrise times, in
`~/nocturne-data/location.toml`:

```toml
latitude = x
longitude = y
timezone = "America/Toronto"
```

## Each morning

```sh
cd tools
python -m nocturne_pc.sync
python -m nocturne_pc.ingest
python -m nocturne_pc.teacher
python -m nocturne_pc.events
python -m nocturne_pc.nights
python -m nocturne_pc.context
```

Nights live in `~/nocturne-data`, encrypted.

## Tests

```sh
pytest tools/tests
python tools/run_host_tests.py    # Linux or macOS
```
