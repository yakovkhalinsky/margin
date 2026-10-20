# Bible Study Mode

Status: **Phase 1+2 implementation (scaffold)**. Navigation and notes work on host tests; not yet
tested on X4 Pro hardware.

## Overview

Bible mode adds a structured Bible reader to margin with:
- **Phase 1**: Book/chapter/verse navigation, jump-to-reference, page rendering
- **Phase 2**: Per-verse notes and highlights stored on SD, designed for BLE sync
- **Future phases**: Cross-references, BLE study chat, daily reading plans

## Data Format

### Bible Text: `/margin/bible/`

The Bible text lives on the SD card in a compact format optimized for low-memory navigation:

```
/margin/bible/
  index.json        # Book metadata and chapter offsets
  GEN.txt           # Genesis text (one book per file)
  EXO.txt           # Exodus
  ...
  REV.txt           # Revelation
```

**index.json** structure:
```json
{
  "version": "BSB",
  "name": "Berean Standard Bible",
  "language": "en",
  "books": [
    {
      "id": "GEN",
      "name": "Genesis",
      "chapters": 50,
      "file": "GEN.txt"
    },
    ...
  ]
}
```

**Book text format** (one file per book):
- UTF-8 encoded, line-based
- Chapter markers: `### 1` (chapter number)
- Verse markers: `1 In the beginning...` (verse number at line start)
- Verses may span multiple lines (continuation lines start with whitespace)
- Empty lines separate paragraphs within a verse

Example `GEN.txt`:
```
### 1
1 In the beginning God created the heavens and the earth.
2 Now the earth was formless and void, and darkness was over
  the surface of the deep. And the Spirit of God was hovering
  over the surface of the waters.
3 And God said, "Let there be light," and there was light.
...
### 2
1 Thus the heavens and the earth were completed in all their
  vast array.
...
```

This format:
- Is human-readable and easy to debug
- Allows streaming reads (no need to load full book)
- Supports seeking to chapter starts via index
- Keeps file sizes manageable (~50-200 KB per book, ~4 MB total)

### Notes and Highlights: `/margin/bible/notes.jsonl`

Per-verse notes and highlights in JSONL (one JSON object per line):

```jsonl
{"ref":"GEN.1.1","type":"highlight","color":"yellow","ts":1729432800,"seq":1}
{"ref":"JHN.3.16","type":"note","text":"Key verse on salvation","ts":1729432900,"seq":2}
{"ref":"PSA.23.1","type":"highlight","color":"blue","ts":1729433000,"seq":3}
{"ref":"PSA.23.1","type":"note","text":"The Lord as shepherd","ts":1729433100,"seq":4}
```

Fields:
- `ref`: OSIS reference (BOOK.chapter.verse)
- `type`: `"highlight"` or `"note"`
- `color`: For highlights: `"yellow"`, `"green"`, `"blue"`, `"pink"` (nullable)
- `text`: For notes: the note content (nullable)
- `ts`: Unix timestamp (seconds)
- `seq`: Monotonic sequence number for sync (device-assigned)
- `deleted`: `true` if soft-deleted (for sync tombstones)

OSIS reference format:
- Book IDs: 3-letter codes from the OSIS standard (GEN, EXO, ..., REV)
- Format: `BOOK.chapter.verse` (e.g., `JHN.3.16` for John 3:16)
- Verse ranges: `BOOK.chapter.start-end` (e.g., `PSA.23.1-6`)

### OSIS Book IDs

| OT | NT |
|----|-----|
| GEN, EXO, LEV, NUM, DEU | MAT, MRK, LUK, JHN, ACT |
| JOS, JDG, RUT, 1SA, 2SA | ROM, 1CO, 2CO, GAL, EPH |
| 1KI, 2KI, 1CH, 2CH, EZR | PHP, COL, 1TH, 2TH, 1TI |
| NEH, EST, JOB, PSA, PRO | 2TI, TIT, PHM, HEB, JAS |
| ECC, SNG, ISA, JER, LAM | 1PE, 2PE, 1JN, 2JN, 3JN |
| EZK, DAN, HOS, JOL, AMO | JUD, REV |
| OBA, JON, MIC, NAH, HAB | |
| ZEP, HAG, ZEC, MAL | |

## Firmware Architecture

### MarginBible Library (`firmware/lib/MarginBible/`)

Pure C++ library (host-testable) providing:

```cpp
namespace margin::bible {

// Reference parsing: "John 3:16" -> {book: JHN, chapter: 3, verse: 16}
struct Reference {
    char book[4];      // OSIS book ID (NUL-terminated)
    uint16_t chapter;
    uint16_t verse;
    uint16_t endVerse; // 0 if single verse, else range end
};

bool parseReference(const char* input, size_t len, Reference& out);
const char* formatReference(const Reference& ref, char* buf, size_t cap);

// Book metadata
struct BookInfo {
    const char* id;    // "GEN", "EXO", etc.
    const char* name;  // "Genesis", "Exodus", etc.
    uint8_t chapters;  // Number of chapters
};
extern const BookInfo kBooks[66];
const BookInfo* findBook(const char* id);
const BookInfo* findBookByName(const char* name, size_t len);

// Navigation state (caller-managed storage)
struct NavState {
    uint8_t bookIndex;   // 0-65
    uint16_t chapter;    // 1-based
    uint16_t verse;      // 1-based, 0 = chapter start
    uint32_t fileOffset; // Byte offset in book file for resume
};

// Notes/highlights (JSONL parsing/writing)
struct Note {
    Reference ref;
    enum Type { Highlight, NoteText } type;
    char color[8];     // "yellow", etc.
    char text[256];    // Note content
    uint32_t ts;       // Unix timestamp
    uint32_t seq;      // Sync sequence
    bool deleted;
};

bool parseNoteLine(const char* line, size_t len, Note& out);
size_t formatNoteLine(const Note& note, char* buf, size_t cap);

}  // namespace margin::bible
```

### MarginBibleApp (`firmware/lib/MarginBibleApp/`)

PapyriX MiniApp for Bible reading:

- **Book/Chapter selector**: Left/Right buttons cycle books, Up/Down cycle chapters
- **Verse display**: Renders current chapter with verse numbers, word-wrapped
- **Jump-to-reference**: Center button opens a reference input (quick-pick or type)
- **Notes indicator**: Highlighted verses show a marker; press to view/edit note
- **Page buttons**: Scroll within chapter or advance to next/prev chapter

### BLE Integration

Bible mode adds these message types to the protocol (see BLE_PROTOCOL.md §7):

| Code | Name | Direction | Purpose |
|------|------|-----------|---------|
| 0x50 | BIBLE_CONTEXT | device → app | Current passage context for chat |
| 0x51 | BIBLE_NOTE_SYNC_REQ | app → device | Request notes changed since seq |
| 0x52 | BIBLE_NOTE_RECORDS | device → app | Batch of note/highlight records |
| 0x53 | BIBLE_NOTE_SYNC_DONE | device → app | Sync batch complete |
| 0x54 | BIBLE_NOTE_PUSH | app → device | Notes edited on phone |
| 0x55 | BIBLE_NOTE_ACK | device → app | Acknowledges pushed notes |

**BIBLE_CONTEXT** TLV body:
- BIBLE_REF (0x60): OSIS reference string (e.g., "JHN.3.16")
- BIBLE_BOOK_NAME (0x61): Full book name (e.g., "John")
- BIBLE_CHAPTER (0x62): u16 chapter number
- BIBLE_VERSE (0x63): u16 verse number
- TEXT (0x03): The verse text (for chat prompt building)

**Note sync** reuses the existing SYNC_* pattern but with Bible-specific RECORD contents.

## Android Integration

### BibleViewModel (`android/.../bible/`)

```kotlin
class BibleNotesViewModel(
    private val ble: MarginBleClient,
    private val db: NotesDatabase,
) : ViewModel() {
    val notes: StateFlow<List<BibleNote>>
    
    fun syncNotes()
    fun addNote(ref: String, text: String)
    fun addHighlight(ref: String, color: String)
    fun deleteNote(ref: String)
}
```

### AskAboutPassageScreen

When the user triggers "Ask about this passage" on the device:
1. Device sends BIBLE_CONTEXT with the current verse
2. App builds a prompt: "Explain [Book Chapter:Verse]: [verse text]"
3. LLM response streams back via existing CHAT_DELTA flow

## Python Converter: `tools/bsb-converter.py`

Downloads and converts the Berean Standard Bible from eBible.org:

```bash
# Download BSB and convert to margin format
python tools/bsb-converter.py --output /path/to/sd/margin/bible/

# Options:
#   --source URL     Override download URL (default: eBible.org USFM)
#   --output DIR     Output directory (required)
#   --format txt     Output format: txt (default) or json
#   --verify         Verify converted output
```

The converter:
1. Downloads the BSB USFM files from eBible.org
2. Parses USFM markers (`\c`, `\v`, `\p`, etc.)
3. Generates `index.json` and per-book `.txt` files
4. Validates chapter/verse counts against known Bible structure

## Roadmap

### Phase 1: Bible Reader (this PR)
- [x] Data format design (index.json, book files, notes.jsonl)
- [x] Reference parser with tests
- [x] Book metadata (66 books, OSIS IDs, names)
- [x] Python converter for BSB
- [ ] MarginBibleApp MiniApp (scaffold, renders static text)
- [ ] Navigation with page buttons

### Phase 2: Notes and Highlights (this PR)
- [x] JSONL notes format design
- [x] Notes parser/writer with tests
- [ ] Highlight rendering in verse display
- [ ] Note viewing on device
- [x] BLE protocol additions documented

### Phase 3: Treasury of Scripture Knowledge (future)
- Cross-reference data format (TSK is public domain)
- Side-panel showing related verses
- Tap to jump to cross-reference

### Phase 4: BLE Study Chat (future)
- "Ask about this verse" triggers BIBLE_CONTEXT → chat flow
- System prompt includes passage context
- LLM answers rendered in Chat screen

### Phase 5: Daily Reading Plan (future)
- Reading plan format (JSON schedule)
- Home screen widget showing today's reading
- Progress tracking synced to phone

## Licence

The Berean Standard Bible is released under a permissive licence by the Berean Bible:
> "The Berean Bible text is free to use in any format for personal, church, or educational
> use, and may be freely distributed provided proper attribution is given."

The converter downloads from eBible.org which hosts the freely-distributable BSB text.
margin does not bundle the Bible text in the repository; users generate it locally.
