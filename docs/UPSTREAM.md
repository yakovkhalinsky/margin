# Upstream base and licensing

## Base: CrossPoint Reader

margin builds on **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)**,
the largest open-source e-reader firmware community. CrossPoint officially supports the Xteink X4 Pro
(ESP32-S3) as of version 1.6.0, with full frontlight controls, swipe gestures, capacitive Home key
support, and USB file transfer.

Pinned commit: `develop` branch (2026-10), at `firmware/upstream/crosspoint-reader`.

**Why CrossPoint over PapyriX:**

margin originally built on PapyriX but migrated to CrossPoint in 2026-10 for several reasons:

| Criterion | CrossPoint | PapyriX |
|-----------|------------|---------|
| X4 Pro support | **Official** (v1.6.0+), user-tested, web flasher | Unofficial, less tested |
| Licence | **MIT throughout** | MIT top-level but includes GPL-3.0 files |
| Community | **8,400+ stars**, 2,000+ forks, active releases | Smaller community |
| Extension model | **Activity system** (clean, Android-like) | MiniApp table (requires patches) |
| Build system | PlatformIO with `env:x4pro` | PlatformIO with `env:x4pro` |
| Flashing | **Web flasher** at crosspointreader.com | Command-line only |

The deciding factor was that the user already runs CrossPoint successfully on their X4 Pro, and
CrossPoint's MIT licence means margin binaries can be distributed without GPL obligations.

## Strategy: pinned submodule + patches

```
firmware/upstream/crosspoint-reader  ← git submodule, pinned to an exact upstream commit
firmware/patches/*.patch             ← margin's hooks (register Bible activity, BLE service)
firmware/platformio.margin.ini       ← margin build configs (loaded via extra_configs)
firmware/lib/Margin*                 ← margin's own libraries (protocol, chat, bible, BLE)
firmware/scripts/setup.sh            ← init submodule, apply patches, link config
```

Why a submodule and not a fork:

1. **One repo for everything.** Firmware, Android app, protocol docs and tests version together.
2. **Clear separation of authorship.** margin's MIT code stays in `firmware/lib/Margin*`; upstream
   code stays in the submodule, unmodified except for visible, reviewable patches.
3. **Cheap upstream updates.** Bumping the pin means: fetch, rebase patches, rebuild, test.

## Extension model: Activities

CrossPoint uses an **Activity-based architecture** similar to Android. Each screen (Home, Library,
Reader, Settings) is an Activity that:

- Extends the `Activity` base class
- Implements `onEnter()`, `onExit()`, `loop()`, `render()`
- Is registered in `ActivityManager`

margin adds its own Activities:
- **BibleActivity**: Structured Bible reading with book/chapter/verse navigation
- **ChatActivity** (future): Agentic chat screen for phone-paired conversations

Activities are added via patches that register them in the ActivityManager's launch table.

## Licences

| Component | Licence | Notes |
|-----------|---------|-------|
| margin's own code (`firmware/lib/Margin*`, `firmware/test`, `android/`, `docs/`, `tools/`) | **MIT** | `LICENSE` at repo root |
| CrossPoint Reader | **MIT** | `firmware/upstream/crosspoint-reader/LICENSE` |
| freeink-sdk (bundled in CrossPoint) | MIT | Display drivers, input handling |
| NimBLE-Arduino | Apache-2.0 | Fetched by PlatformIO |
| Arduino-ESP32 / ESP-IDF | Apache-2.0 / LGPL-2.1 | Framework, fetched by PlatformIO |

**Key advantage over PapyriX:** CrossPoint is MIT throughout. There are no GPL-3.0 files in the
build, so margin firmware binaries can be distributed under MIT terms without source obligations
beyond what margin already provides (this public repo).

## Historical note: PapyriX

margin originally built on [PapyriX](https://github.com/bigbag/papyrix-reader) by Pavel Liashkov.
PapyriX is MIT-licensed at the top level but includes GPL-3.0-or-later files (`lib/Group5`,
`lib/GfxRenderer/src/OrderedDither.*`) that complicated binary distribution. The migration to
CrossPoint happened when it became clear that:

1. CrossPoint's X4 Pro support was production-ready (v1.6.0)
2. The user's device already ran CrossPoint successfully
3. CrossPoint's MIT-only licensing simplified distribution
4. CrossPoint's Activity model was cleaner than PapyriX's MiniApp table

The margin libraries (`MarginProtocol`, `MarginChat`, `MarginBible`, `MarginBle`) were designed
base-agnostic, so the migration required only rewriting the app integration layer.

## What "inspired by Snail OS" means

Snail OS is closed-source, sold as signed firmware. margin does not use, decompile or redistribute
any of it; it only borrows the product idea of an e-ink reader paired with a phone companion app.
