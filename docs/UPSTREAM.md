# Upstream base and licensing

## Base: PapyriX

margin builds on **[PapyriX](https://github.com/bigbag/papyrix-reader)** by Pavel Liashkov, open-source
reader firmware for Xteink X3, X4, X4 Pro and X4 v2 Classic. It already has a dedicated X4 Pro
target (`env:x4pro` / `env:release_x4pro`, artifact `papyrix-x4pro.bin`, `make flash-x4pro`, app
image at `0x10000`), runtime panel-controller detection, GT911 touch, front light, and EPUB / FB2 /
Markdown / TXT / HTML / XTC reading with custom fonts and themes. Its X4 Pro image ships **without
Bluetooth**, so margin adds the BLE side.

Pinned commit: `ee071b173e29b1e81fc7e259260077d5ba981b38` (2026-10-07, PapyriX 1.33.1-dev), at
`firmware/upstream/papyrix-reader`.

**Alternative: [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)**
(MIT). It is the largest community firmware for these readers, with an active codebase and web flasher
tooling, but its X4 Pro support was still being worked out when margin started. PapyriX already
vendors CrossPoint-derived pieces (for example its patched SdFat). If PapyriX stalls, CrossPoint is
the fallback base; the margin libraries (`MarginProtocol`, `MarginChat`, `MarginBle`) are written
to be base-agnostic, and only `MarginApp` and the hook patches are PapyriX-specific.

### CrossPoint Reader Evaluation (2026-10)

Some users report success running CrossPoint Reader on the X4 Pro. An evaluation was done to decide
whether margin should rebase onto CrossPoint instead of PapyriX:

**CrossPoint advantages:**
- Reportedly good X4 Pro support for users who have tested it
- MIT licence throughout (no GPL-3.0 files that complicate binary distribution)
- Web flasher tooling for easier installation
- Active community development

**PapyriX advantages:**
- margin's existing integration (patches, build config, tests) is already working
- Stable pinned commit with known behaviour
- Well-documented X4 Pro target (`env:x4pro`) in the build system
- Panel controller auto-detection already handles UC8179/UC8279/SSD1677 variance

**Decision:** Stay on PapyriX for now. The migration risk outweighs the benefits at this stage:
- margin's libraries are already base-agnostic by design
- Only `MarginApp` and the hook patches are PapyriX-specific
- A full migration would require rewriting the app integration and testing on hardware
- No blocking issues have been encountered with PapyriX

**Migration plan (if needed later):**
1. Create `margin-crosspoint` branch for experimentation
2. Port `MarginApp` to CrossPoint's app model (likely similar to PapyriX's MiniApp)
3. Adapt BLE service initialization to CrossPoint's startup sequence
4. Test all features on X4 Pro hardware
5. If successful, switch the submodule URL in `.gitmodules`

This decision will be revisited if:
- PapyriX development stalls or breaks X4 Pro support
- CrossPoint gains features that significantly benefit margin (e.g., better partial refresh)
- Users report PapyriX-specific issues on their devices

## Strategy: pinned submodule + small patch series (not a long-lived fork)

```
firmware/upstream/papyrix-reader   ← git submodule, pinned to an exact upstream commit
firmware/patches/*.patch           ← margin's hooks into upstream (today: 1 patch, +9 lines)
firmware/platformio.margin.ini     ← linked in as platformio.local.ini (PapyriX loads it via extra_configs)
firmware/lib/Margin*               ← all of margin's own code, pulled in with symlink:// lib_deps
firmware/scripts/setup.sh          ← init submodule, reset to pin, apply patches, link the ini
firmware/scripts/rebase-upstream.sh← move the pin and check every patch still applies
```

Why this and not a fork:

1. **One repo for everything.** Firmware, Android app, protocol docs and tests version together; a
   protocol change lands in one commit on both sides.
2. **Clear separation of authorship and licence.** Everything under `firmware/lib/Margin*`,
   `android/` and `docs/` is margin's own MIT code; upstream code stays in the submodule, unmodified
   except for the visible, reviewable patches. That makes the GPL obligations below easy to meet.
3. **Cheap upstream updates.** PapyriX moves fast (its changelog is ~90 KB). Bumping the pin means
   `rebase-upstream.sh`, rebuild, test on the device, commit the new submodule SHA. Conflicts can
   only happen in the patch files, which are tiny on purpose.
4. **PapyriX already has the extension points we need.** `platformio.local.ini` via
   `extra_configs` (no build-config patch needed) and the `MiniApp` table (a one-file hook for the
   Chat screen).

When to switch to a fork: if the patch series grows past roughly a handful of files or needs deep
changes (Reader text selection for M5, a background BLE service in the main loop), create
`yakovkhalinsky/papyrix-reader` with a `margin` branch, point the submodule URL at it, and rebase that
branch on upstream. The layout above stays the same; only `.gitmodules` changes. Generic hooks
(e.g. app registration, a "share selection" callback) should be offered upstream as PRs first.

## Licences

| Component | Licence | Notes |
|---|---|---|
| margin's own code (`firmware/lib/Margin*`, `firmware/test`, `firmware/scripts`, `firmware/patches`, `android/`, `docs/`) | **MIT** | `LICENSE` at the repo root |
| PapyriX (top level) | MIT, © 2026 Pavel Liashkov | `firmware/upstream/papyrix-reader/LICENSE` |
| PapyriX `lib/BoardSupport`, `lib/EInkDisplay`, `lib/BatteryMonitor`, `lib/SDCardManager` | MIT, © 2026 FreeInk | selective ports, see each `FREEINK_LICENSE` |
| PapyriX `lib/SdFat` | MIT (Bill Greiman), with **GPL-3.0** DigitalIO files under `src/SdCard/SdSpiCard/SpiDriver/DigitalIO` | per its `library.json` |
| PapyriX `lib/Group5` (Larry Bank) | **GPL-3.0-or-later** | CCITT G5 codec |
| PapyriX `lib/GfxRenderer/src/OrderedDither.*` (from bb_epaper) | **GPL-3.0-or-later** | used by the renderer, so it is in every image |
| NimBLE-Arduino (h2zero) | Apache-2.0 | fetched by PlatformIO, not vendored |
| Arduino-ESP32 / ESP-IDF | LGPL-2.1 / Apache-2.0 | toolchain and framework, fetched by PlatformIO |

PapyriX's top-level licence is MIT, but its tree contains GPL-3.0-or-later files that are compiled
into the X4 Pro firmware (at least the ordered-dither code in `GfxRenderer`). What that means:

- **Source code:** margin's own files stay **MIT**. MIT is GPL-compatible, so combining is fine, and
  nobody needs to relicense margin's code.
- **Firmware binaries:** a compiled `margin-x4pro.bin` is a combined work that includes GPL-3.0
  code, so **anyone who distributes the binary must do so under GPL-3.0 terms**: provide the
  complete corresponding source (this repo at the tagged commit, *with* the pinned submodule and
  patches) and the licence texts, and not add restrictions. Publishing release binaries from this
  public repo with a matching tag satisfies that. Flashing it on your own device is unaffected.
- Keep upstream licence files and copyright headers intact; do not copy upstream files into
  `firmware/lib/Margin*`. If an upstream file must be changed, change it through a patch so its
  licence stays with it.
- Releases should include a `THIRD_PARTY_LICENSES` bundle (TODO, M1) generated from the files above.

This is a practical reading, not legal advice.

## What "inspired by Snail OS" means

Snail OS is closed-source, sold as signed firmware. margin does not use, decompile or redistribute
any of it; it only borrows the product idea of an e-ink reader paired with a phone companion app.
