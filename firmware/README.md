# margin firmware

CrossPoint Reader (pinned submodule) + margin's own libraries, built with PlatformIO for the
**Xteink X4 Pro (ESP32-S3)**. Read the [flash instructions](#flashing) before flashing.

## Status

| Piece | State |
|---|---|
| `lib/MarginProtocol` (framing, reassembly, CTRL, TLV, CRC32, message codecs) | Real code, 22 host tests pass |
| `lib/MarginChat` (ChatModel, wrapText, RefreshPolicy) | Real code, host-tested |
| `lib/MarginBible` (reference parsing, book metadata, notes handling) | Real code, 17 host tests pass |
| `lib/MarginBle` (NimBLE-Arduino GATT server) | Compiles; **never run on a device** |
| `lib/MarginBibleActivity` (Bible reader as CrossPoint Activity) | Scaffold; compiles; **never run on a device** |
| `margin_x4pro` env | **Builds** with `pio run -e margin_x4pro`. Not flashed yet |

## Build

```bash
pip install platformio                         # or use nix/uv
cd /workspace/firmware/upstream/crosspoint-reader
pio run -e margin_x4pro                        # margin build for X4 Pro
```

Host tests (no hardware, no PlatformIO):
```bash
make -C firmware/test
```

## Flashing

> **Back up your device first!** See [HARDWARE.md](../docs/HARDWARE.md#flashing-and-recovery).

### Option 1: Command line (recommended)

```bash
# Connect pogo-pin dongle, device powered on
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
    write_flash 0x10000 .pio/build/margin_x4pro/firmware.bin
```

### Option 2: CrossPoint web flasher

1. Build the firmware: `pio run -e margin_x4pro`
2. Go to https://crosspointreader.com/#flash-tools
3. Select "X4 Pro" and "Custom .bin"
4. Upload `.pio/build/margin_x4pro/firmware.bin`

### Reverting to stock CrossPoint

Use the same web flasher at https://crosspointreader.com/#flash-tools and select an official
CrossPoint release instead of "Custom .bin".

## Layout

```
lib/MarginProtocol/       Protocol: framing, TLV, messages, CRC32  (host-tested)
lib/MarginChat/           Chat: ChatModel, wrapText, RefreshPolicy (host-tested)
lib/MarginBible/          Bible: reference parser, book metadata   (host-tested)
lib/MarginBle/            BLE: NimBLE GATT server                  (device only)
lib/MarginBibleActivity/  Bible Activity for CrossPoint            (device only)
patches/                  Patches to register margin activities
scripts/                  setup.sh, rebase-upstream.sh
platformio.margin.ini     margin build environments
upstream/crosspoint-reader/  CrossPoint Reader submodule
test/                     Host unit tests
```

## Integration with CrossPoint

margin extends CrossPoint using its Activity system:

1. **BibleActivity**: Structured Bible reader with book/chapter/verse navigation
2. **ChatActivity** (future): Agentic chat screen for phone conversations

Activities are registered via patches to `ActivityManager.cpp` and `HomeActivity.cpp`.

## What's verified vs unverified

| Component | Status |
|-----------|--------|
| Host tests (protocol, chat, bible) | **39 tests pass** |
| Python converter | **15 tests pass** |
| margin_x4pro build | **Compiles** |
| Bible activity on device | **Unverified** (never flashed) |
| BLE service on device | **Unverified** |
| Chat screen on device | **Unverified** |
