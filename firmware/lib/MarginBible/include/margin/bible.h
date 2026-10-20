// SPDX-License-Identifier: MIT
// Bible reference parsing, book metadata, and notes handling for margin.
// Pure C++17, no platform dependencies (host-testable).
#pragma once

#include <cstddef>
#include <cstdint>

namespace margin::bible {

// OSIS book IDs (3-letter codes)
struct BookInfo {
    const char* id;       // "GEN", "EXO", etc.
    const char* name;     // "Genesis", "Exodus", etc.
    const char* abbrev;   // Common abbreviation for parsing ("Gen", "Ex", etc.)
    uint8_t chapters;     // Number of chapters in this book
    bool isNT;            // true for New Testament books
};

extern const BookInfo kBooks[66];
constexpr size_t kBookCount = 66;
constexpr size_t kOTBookCount = 39;
constexpr size_t kNTBookCount = 27;

const BookInfo* findBook(const char* id);
const BookInfo* findBookByName(const char* name, size_t len);
const BookInfo* findBookByIndex(size_t index);
int bookIndex(const char* id);

// Bible reference: book, chapter, verse (or verse range)
struct Reference {
    char book[4];         // OSIS book ID, NUL-terminated
    uint16_t chapter;     // 1-based
    uint16_t verse;       // 1-based, 0 means whole chapter
    uint16_t endVerse;    // 0 if single verse, else end of range

    bool isValid() const;
    bool isChapterRef() const { return verse == 0; }
    bool isRange() const { return endVerse > 0 && endVerse > verse; }
};

// Parse a reference like "John 3:16", "Jn 3:16", "JHN.3.16", "Genesis 1:1-3"
// Returns true on success, filling out `ref`.
// Handles common formats:
//   - "John 3:16" (name chapter:verse)
//   - "Jn 3:16" (abbreviation)
//   - "JHN.3.16" (OSIS format)
//   - "John 3" (chapter only)
//   - "John 3:16-18" (verse range)
//   - "1 John 1:1" (books with number prefix)
bool parseReference(const char* input, size_t len, Reference& out);

// Format a reference to a buffer. Returns bytes written (excluding NUL).
// Examples: "John 3:16", "Genesis 1:1-3", "Psalms 23"
size_t formatReference(const Reference& ref, char* buf, size_t cap);

// Format as OSIS: "JHN.3.16"
size_t formatOsis(const Reference& ref, char* buf, size_t cap);

// Navigation state for reading
struct NavState {
    uint8_t bookIndex;    // 0-65 (index into kBooks)
    uint16_t chapter;     // 1-based
    uint16_t topVerse;    // First visible verse (for scroll position)
    uint32_t fileOffset;  // Byte offset in book file (for resume after power off)
};

// Notes and highlights
enum class NoteType : uint8_t {
    Highlight = 0,
    Note = 1,
};

enum class HighlightColor : uint8_t {
    Yellow = 0,
    Green = 1,
    Blue = 2,
    Pink = 3,
};

struct Note {
    Reference ref;
    NoteType type;
    HighlightColor color; // Only for Highlight type
    char text[256];       // Only for Note type, NUL-terminated
    uint32_t timestamp;   // Unix timestamp (seconds)
    uint32_t seq;         // Monotonic sequence for sync
    bool deleted;         // Soft-delete tombstone

    Note() : type(NoteType::Highlight), color(HighlightColor::Yellow),
             text{}, timestamp(0), seq(0), deleted(false) {
        ref = {};
    }
};

// Parse a single line from notes.jsonl. Returns true on success.
// The line should NOT include the trailing newline.
bool parseNoteLine(const char* line, size_t len, Note& out);

// Format a note as a JSONL line (no trailing newline).
// Returns bytes written, or 0 if buffer too small.
size_t formatNoteLine(const Note& note, char* buf, size_t cap);

// Color name helpers
const char* colorName(HighlightColor c);
bool parseColor(const char* name, size_t len, HighlightColor& out);

}  // namespace margin::bible
