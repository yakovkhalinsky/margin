# Roadmap

Each milestone ends with something you can try on the device. ✅ done · 🟡 scaffolded · ⬜ not started.

## M0: Scaffold ✅ / 🟡

- ✅ Repo, docs, PapyriX pinned as a submodule, hook patch, PlatformIO env `margin_x4pro`.
- ✅ Protocol library (C++) and host tests; Kotlin twin with JVM tests; shared golden vectors.
- ✅ `margin_x4pro` **compiles and links** on a dev box (not yet flashed).
- 🟡 NimBLE service, Chat MiniApp, Android skeleton: code exists, untested on hardware.

## M1: Boot the base reader on the X4 Pro ⬜

- Back up the 16 MB stock flash (see [HARDWARE.md](HARDWARE.md#flashing-and-recovery)), keep two copies.
- Flash plain upstream PapyriX `release_x4pro` first; confirm display driver selection (which
  controller your unit has), touch, front light, SD, buttons, battery.
- Then flash `margin_x4pro`; confirm the reader still behaves exactly like upstream and "Chat"
  appears in Apps.
- Record your unit's panel controller and anything odd in HARDWARE.md.

## M2: BLE service + echo ⬜

- NimBLE advertising, pairing window from a Settings action, numeric comparison on the e-ink screen.
- Bonded-only access; verify with nRF Connect that an unbonded phone cannot read or write anything.
- Device echoes CHAT_DELTA back as USER_MESSAGE (debug build only); measure throughput at MTU 247.
- Accept-list advertising for bonded peers; RPA resolution; reconnect after sleep/wake.
- Android: scan by service UUID / CompanionDeviceManager, bond, serialized GATT op queue, ACK timeouts.
- Decide the background BLE policy and measure idle drain (target: < 1 %/hour while advertising).

## M3: Chat screen with partial refresh ⬜

- Bottom-anchored transcript, scrolling with page buttons and swipe, per-turn line-break cache.
- Touch keyboard (QWERTY + symbols) and quick-reply chips; long-press to cancel.
- Refresh tuning: coalesced fast refreshes; evaluate Half vs Full cleanup; research windowed partial
  updates on UC8179/UC8279 (only SSD1677 has a Window mode in PapyriX today).
- "Thinking" indicator (text; optional front-light pulse, off by default).
- Own `StateId` for Chat + Home shortcut, if the MiniApp model gets in the way.

## M4: Android app MVP with an LLM ⬜

- Settings: provider (OpenAI-compatible / Ollama), base URL, model, API key in Keystore.
- Streaming with delta coalescing; TextPrep that is stable across deltas; history in Room.
- Tool calling (e.g. web fetch, calendar "summarise my day"), surfaced as THINKING_STATUS labels.
- Foreground service so a conversation continues with the screen off.

## M5: Passage share ⬜

- Text selection in PapyriX's Reader (touch drag or button-driven word cursor).
- Intent menu: Explain / Summarise selection / Summarise chapter / Define / Translate / Ask.
- PASSAGE_SHARE with book title, chapter, spine/offset/percent; answer opens in Chat with a
  "back to page" action.

## M6: File push ⬜

- FileReceiver on the device (`/margin/inbox`, `.part` + CRC32 + rename, resume).
- Android share-sheet target: EPUB/FB2/TXT/MD as-is; web articles converted to Markdown or EPUB.
- Library shows an "Inbox" shelf.

## M7: Bookmark and highlight sync ⬜

- Device change log (`seq`) for bookmarks, highlights (new in margin), and reading progress, built on
  PapyriX's `BookmarkManager` / `ProgressManager`.
- SYNC_REQUEST/RECORDS/DONE/ACK; phone-side store with export (Markdown / Readwise-style CSV).
- Later: two-way sync (notes edited on the phone).

## M8: Bible Study Mode (Phase 1+2) 🟡

- 🟡 Bible data format: index.json + per-book .txt files on SD (`/margin/bible/`)
- 🟡 BSB converter: Python tool to download/convert Berean Standard Bible from eBible.org
- 🟡 Reference parser: "John 3:16" → book/chapter/verse, OSIS format support
- 🟡 Book metadata: 66 books with OSIS IDs, names, abbreviations, chapter counts
- 🟡 Notes format: JSONL keyed by OSIS reference for highlights and notes
- 🟡 BLE protocol: BIBLE_CONTEXT and note sync messages (0x50-0x55)
- 🟡 Android stubs: BibleNotesViewModel, BibleStudyChat prompt building
- ⬜ MarginBibleApp: book/chapter/verse navigation with buttons
- ⬜ Highlight rendering in verse display
- ⬜ Note viewing/editing on device

## M9: Bible Study Mode (Phase 3+4) ⬜

- Treasury of Scripture Knowledge cross-references (TSK is public domain)
- Side-panel showing related verses, tap to jump
- "Ask about this verse" → BIBLE_CONTEXT → chat flow
- System prompt includes passage context
- LLM answers rendered in Chat screen

## M10: Daily Reading Plan ⬜

- Reading plan format (JSON schedule, canonical plans like M'Cheyne)
- Home screen widget showing today's reading
- Progress tracking synced to phone
- Optional notifications/reminders

## Later / ideas

- Upstream generic hooks to PapyriX (e.g. an app-registration extension point) to shrink margin's patch set.
- CI: GitHub Actions running `make -C firmware/test`, the Kotlin protocol tests and a `pio run -e margin_x4pro` build.
- OTA updates pushed from the phone over BLE (PapyriX already has an OTA partition layout).
- iOS client (the protocol is platform-neutral).
- Bible translations: support for multiple translations with switchable display
- Audio Bible integration: BLE streaming from phone to device speaker
