# margin firmware

PapyriX (pinned submodule) + margin's own libraries, built with PlatformIO for the **Xteink X4 Pro
(ESP32-S3)**. Read [../docs/HARDWARE.md](../docs/HARDWARE.md#flashing-and-recovery) before flashing.

## Status

| Piece | State |
|---|---|
| `lib/MarginProtocol` (framing, reassembly, CTRL, TLV, CRC32, message codecs, file-name checks) | Real code, 22 host tests pass (`make -C firmware/test`, ASan + UBSan) |
| `lib/MarginChat` (ChatModel, wrapText, RefreshPolicy) | Real code, host-tested |
| `lib/MarginBle` (NimBLE-Arduino 2.5.x GATT server) | Compiles for `margin_x4pro`; **never run on a device** |
| `lib/MarginApp` (Chat screen as a PapyriX MiniApp) | Compiles; **never run on a device**; many TODO(Mx) |
| `margin_x4pro` env | **Builds and links** (pioarduino 55.03.37, Arduino-ESP32 3.3.7). Not flashed yet |

Build size vs upstream `x4pro` at the same pin: flash 4,631,370 B vs 4,443,882 B (+187 KB, 70.7 % of
the 6.25 MB app slot); static RAM 137,588 B vs 128,816 B (+8.8 KB). Large buffers are in PSRAM at runtime.

## Build

```bash
pip install platformio                       # or use the PapyriX nix shell / uv env
firmware/scripts/setup.sh                    # submodule at the pin, apply patches, link the ini
cd firmware/upstream/papyrix-reader
pio run -e margin_x4pro                      # dev build (serial log level 2)
pio run -e margin_release_x4pro              # release flavour
```

Host tests (no hardware, no PlatformIO): `make -C firmware/test`.

## Flash (only after a full backup)

```bash
# Pogo dongle connected, hold Power for the whole operation.
pio run -e margin_x4pro --target upload
# or with esptool:
esptool --chip esp32s3 --port /dev/ttyACM0 write-flash 0x10000 .pio/build/margin_x4pro/firmware.bin
```

## Integration plan

How margin plugs into PapyriX, in order of increasing intrusiveness:

1. **Build config, no patch.** PapyriX's `platformio.ini` has `extra_configs = platformio.local.ini`.
   `setup.sh` symlinks `platformio.margin.ini` there. It defines `env:margin_x4pro` /
   `env:margin_release_x4pro` (extending upstream's X4 Pro envs), adds `-DMARGIN_ENABLE_CHAT=1`,
   NimBLE-Arduino, and margin's libraries via `symlink://../../lib/...`.
2. **Chat screen, patch 0001 (+9 lines).** `src/apps/AppRegistry.cpp` gets a `"Chat"` entry
   (appended last, so upstream `APP_*` indices stay valid), guarded by `MARGIN_ENABLE_CHAT`. Upstream
   builds are unaffected.
3. **Planned hooks (M2–M7).** Each becomes one small patch, ideally upstreamed as a generic
   extension point:
   - `0002` Settings → "Phone link": BLE on/off, Pair phone (opens a 60 s pairing window), Forget phone.
   - `0003` main-loop tick for `margin::ble::service().poll()` so BLE works outside the Chat screen.
   - `0004` `StateId::Chat` + Home shortcut (if the MiniApp model is too limiting).
   - `0005` Reader text selection → "Ask about this" (PASSAGE_SHARE). The largest change; candidate
     for an upstream "selection action" callback.
   - `0006` Library shelf for `/margin/inbox` (file push).
   - `0007` bookmark/highlight change log on top of `BookmarkManager` / `ProgressManager` (sync).
4. **Upstream's feature guard.** `scripts/check_target_features.py` in PapyriX rejects "Bluetooth"
   symbols for its own x4pro release artifact. It only runs in PapyriX's packaging script for upstream
   targets; margin's envs are separate. If margin ever reuses `package_firmware.py`, add a `margin_x4pro`
   target there that allows NimBLE.

Moving the pin: `firmware/scripts/rebase-upstream.sh [ref]`, then `setup.sh`, build, test on
hardware, and commit the submodule bump with the refreshed patches.

## Layout

```
lib/MarginProtocol/   include/margin/{protocol,framing,tlv,messages,crc32}.h + src/
lib/MarginChat/       include/margin/chat_model.h + src/
lib/MarginBle/        include/margin/ble_service.h + src/ble_service.cpp   (NimBLE)
lib/MarginApp/        src/MarginChatApp.{h,cpp}                            (PapyriX MiniApp)
patches/              0001-register-margin-chat-app.patch
scripts/              setup.sh, rebase-upstream.sh
test/                 host unit tests (Makefile, no external deps)
platformio.margin.ini
upstream/papyrix-reader/  (submodule)
```

## Known gaps, honestly

- Nothing has run on an X4 Pro yet: no evidence that advertising, pairing, notifications or the
  Chat screen work on hardware.
- Pairing window opening, accept-list advertising and RPA handling are TODO (M2). Until then, unbonded
  centrals can *connect* but cannot access any attribute, and pairing is refused outside the window,
  which is never opened in the scaffold.
- The Chat screen relies on `AppLauncherState` to push frames with FAST_REFRESH; `RefreshPolicy`'s
  Full/Half cleanup is computed but not applied yet.
- Touch keyboard, scrolling layout, file receiver and sync are not implemented.
