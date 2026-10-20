// SPDX-License-Identifier: MIT
#include <cstring>

#include "margin/bible.h"
#include "test.h"

using namespace margin::bible;

// Reference parsing tests
TEST(parse_reference_john_3_16) {
    Reference ref;
    CHECK(parseReference("John 3:16", 9, ref));
    CHECK_EQ(std::string(ref.book), "JHN");
    CHECK_EQ(ref.chapter, 3u);
    CHECK_EQ(ref.verse, 16u);
}

TEST(parse_reference_basic_formats) {
    Reference ref;

    // Full name with chapter:verse
    CHECK(parseReference("John 3:16", 9, ref));
    CHECK_EQ(std::string(ref.book), "JHN");
    CHECK_EQ(ref.chapter, 3u);
    CHECK_EQ(ref.verse, 16u);
    CHECK_EQ(ref.endVerse, 0u);

    // Abbreviation
    CHECK(parseReference("Jn 3:16", 7, ref));
    CHECK_EQ(std::string(ref.book), "JHN");
    CHECK_EQ(ref.chapter, 3u);
    CHECK_EQ(ref.verse, 16u);

    // OSIS format
    CHECK(parseReference("JHN.3.16", 8, ref));
    CHECK_EQ(std::string(ref.book), "JHN");
    CHECK_EQ(ref.chapter, 3u);
    CHECK_EQ(ref.verse, 16u);

    // Chapter only
    CHECK(parseReference("John 3", 6, ref));
    CHECK_EQ(std::string(ref.book), "JHN");
    CHECK_EQ(ref.chapter, 3u);
    CHECK_EQ(ref.verse, 0u);

    // Verse range
    CHECK(parseReference("Genesis 1:1-3", 13, ref));
    CHECK_EQ(std::string(ref.book), "GEN");
    CHECK_EQ(ref.chapter, 1u);
    CHECK_EQ(ref.verse, 1u);
    CHECK_EQ(ref.endVerse, 3u);
}

TEST(parse_reference_numbered_books) {
    Reference ref;

    // "1 John 1:1"
    CHECK(parseReference("1 John 1:1", 10, ref));
    CHECK_EQ(std::string(ref.book), "1JN");
    CHECK_EQ(ref.chapter, 1u);
    CHECK_EQ(ref.verse, 1u);

    // "2 Samuel 7:12"
    CHECK(parseReference("2 Samuel 7:12", 13, ref));
    CHECK_EQ(std::string(ref.book), "2SA");
    CHECK_EQ(ref.chapter, 7u);
    CHECK_EQ(ref.verse, 12u);

    // "1Sam 1:1"
    CHECK(parseReference("1Sam 1:1", 8, ref));
    CHECK_EQ(std::string(ref.book), "1SA");
    CHECK_EQ(ref.chapter, 1u);
    CHECK_EQ(ref.verse, 1u);

    // "1 Chronicles 1:1"
    CHECK(parseReference("1 Chronicles 1:1", 16, ref));
    CHECK_EQ(std::string(ref.book), "1CH");

    // "3 John 1:1"
    CHECK(parseReference("3 John 1:1", 10, ref));
    CHECK_EQ(std::string(ref.book), "3JN");
}

TEST(parse_reference_various_abbreviations) {
    Reference ref;

    // Common abbreviations
    CHECK(parseReference("Gen 1:1", 7, ref));
    CHECK_EQ(std::string(ref.book), "GEN");

    CHECK(parseReference("Ex 20:1", 7, ref));
    CHECK_EQ(std::string(ref.book), "EXO");

    CHECK(parseReference("Ps 23:1", 7, ref));
    CHECK_EQ(std::string(ref.book), "PSA");

    CHECK(parseReference("Matt 5:1", 8, ref));
    CHECK_EQ(std::string(ref.book), "MAT");

    CHECK(parseReference("Rev 1:1", 7, ref));
    CHECK_EQ(std::string(ref.book), "REV");
}

TEST(parse_reference_osis_format) {
    Reference ref;

    CHECK(parseReference("GEN.1.1", 7, ref));
    CHECK_EQ(std::string(ref.book), "GEN");
    CHECK_EQ(ref.chapter, 1u);
    CHECK_EQ(ref.verse, 1u);

    CHECK(parseReference("PSA.23.1-6", 10, ref));
    CHECK_EQ(std::string(ref.book), "PSA");
    CHECK_EQ(ref.chapter, 23u);
    CHECK_EQ(ref.verse, 1u);
    CHECK_EQ(ref.endVerse, 6u);

    // Chapter only in OSIS
    CHECK(parseReference("MAT.5", 5, ref));
    CHECK_EQ(std::string(ref.book), "MAT");
    CHECK_EQ(ref.chapter, 5u);
    CHECK_EQ(ref.verse, 0u);
}

TEST(parse_reference_invalid) {
    Reference ref;

    // Unknown book
    CHECK(!parseReference("Foobar 1:1", 10, ref));

    // Invalid chapter
    CHECK(!parseReference("Genesis 100:1", 13, ref));  // Genesis has 50 chapters

    // Empty input
    CHECK(!parseReference("", 0, ref));
    CHECK(!parseReference(nullptr, 0, ref));
}

TEST(format_reference) {
    char buf[64];

    Reference ref;
    std::memcpy(ref.book, "JHN", 4);
    ref.chapter = 3;
    ref.verse = 16;
    ref.endVerse = 0;

    size_t n = formatReference(ref, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK_EQ(std::string(buf), "John 3:16");

    // Chapter only
    ref.verse = 0;
    n = formatReference(ref, buf, sizeof(buf));
    CHECK_EQ(std::string(buf), "John 3");

    // Range
    ref.verse = 1;
    ref.endVerse = 3;
    std::memcpy(ref.book, "GEN", 4);
    ref.chapter = 1;
    n = formatReference(ref, buf, sizeof(buf));
    CHECK_EQ(std::string(buf), "Genesis 1:1-3");
}

TEST(format_osis) {
    char buf[32];

    Reference ref;
    std::memcpy(ref.book, "JHN", 4);
    ref.chapter = 3;
    ref.verse = 16;
    ref.endVerse = 0;

    size_t n = formatOsis(ref, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK_EQ(std::string(buf), "JHN.3.16");

    ref.verse = 0;
    n = formatOsis(ref, buf, sizeof(buf));
    CHECK_EQ(std::string(buf), "JHN.3");
}

// Book lookup tests
TEST(find_book_by_id) {
    const BookInfo* b = findBook("GEN");
    CHECK(b != nullptr);
    CHECK_EQ(std::string(b->name), "Genesis");
    CHECK_EQ(b->chapters, 50u);
    CHECK(!b->isNT);

    b = findBook("JHN");
    CHECK(b != nullptr);
    CHECK_EQ(std::string(b->name), "John");
    CHECK(b->isNT);

    b = findBook("XXX");
    CHECK(b == nullptr);
}

TEST(find_book_by_name) {
    const BookInfo* b = findBookByName("Genesis", 7);
    CHECK(b != nullptr);
    CHECK_EQ(std::string(b->id), "GEN");

    b = findBookByName("John", 4);
    CHECK(b != nullptr);
    CHECK_EQ(std::string(b->id), "JHN");

    b = findBookByName("Jn", 2);
    CHECK(b != nullptr);
    CHECK_EQ(std::string(b->id), "JHN");

    b = findBookByName("Foobar", 6);
    CHECK(b == nullptr);
}

TEST(book_count) {
    CHECK_EQ(kBookCount, 66u);
    CHECK_EQ(kOTBookCount, 39u);
    CHECK_EQ(kNTBookCount, 27u);

    // Verify NT/OT split
    size_t otCount = 0, ntCount = 0;
    for (size_t i = 0; i < kBookCount; ++i) {
        if (kBooks[i].isNT) ++ntCount;
        else ++otCount;
    }
    CHECK_EQ(otCount, kOTBookCount);
    CHECK_EQ(ntCount, kNTBookCount);
}

// Notes parsing tests
TEST(parse_note_highlight) {
    Note note;
    const char* line = R"({"ref":"GEN.1.1","type":"highlight","color":"yellow","ts":1729432800,"seq":1})";
    CHECK(parseNoteLine(line, std::strlen(line), note));
    CHECK_EQ(std::string(note.ref.book), "GEN");
    CHECK_EQ(note.ref.chapter, 1u);
    CHECK_EQ(note.ref.verse, 1u);
    CHECK(note.type == NoteType::Highlight);
    CHECK(note.color == HighlightColor::Yellow);
    CHECK_EQ(note.timestamp, 1729432800u);
    CHECK_EQ(note.seq, 1u);
    CHECK(!note.deleted);
}

TEST(parse_note_text) {
    Note note;
    const char* line = R"({"ref":"JHN.3.16","type":"note","text":"Key verse on salvation","ts":1729432900,"seq":2})";
    CHECK(parseNoteLine(line, std::strlen(line), note));
    CHECK_EQ(std::string(note.ref.book), "JHN");
    CHECK_EQ(note.ref.chapter, 3u);
    CHECK_EQ(note.ref.verse, 16u);
    CHECK(note.type == NoteType::Note);
    CHECK_EQ(std::string(note.text), "Key verse on salvation");
    CHECK_EQ(note.timestamp, 1729432900u);
    CHECK_EQ(note.seq, 2u);
}

TEST(parse_note_deleted) {
    Note note;
    const char* line = R"({"ref":"PSA.23.1","type":"highlight","color":"blue","ts":1729433000,"seq":3,"deleted":true})";
    CHECK(parseNoteLine(line, std::strlen(line), note));
    CHECK(note.deleted);
}

TEST(format_note_highlight) {
    Note note;
    std::memcpy(note.ref.book, "GEN", 4);
    note.ref.chapter = 1;
    note.ref.verse = 1;
    note.ref.endVerse = 0;
    note.type = NoteType::Highlight;
    note.color = HighlightColor::Green;
    note.timestamp = 1000;
    note.seq = 5;
    note.deleted = false;

    char buf[256];
    size_t n = formatNoteLine(note, buf, sizeof(buf));
    CHECK(n > 0);

    // Parse it back
    Note parsed;
    CHECK(parseNoteLine(buf, n, parsed));
    CHECK_EQ(std::string(parsed.ref.book), "GEN");
    CHECK_EQ(parsed.ref.chapter, 1u);
    CHECK_EQ(parsed.ref.verse, 1u);
    CHECK(parsed.type == NoteType::Highlight);
    CHECK(parsed.color == HighlightColor::Green);
    CHECK_EQ(parsed.timestamp, 1000u);
    CHECK_EQ(parsed.seq, 5u);
    CHECK(!parsed.deleted);
}

TEST(format_note_text) {
    Note note;
    std::memcpy(note.ref.book, "JHN", 4);
    note.ref.chapter = 3;
    note.ref.verse = 16;
    note.ref.endVerse = 0;
    note.type = NoteType::Note;
    std::strcpy(note.text, "Test note");
    note.timestamp = 2000;
    note.seq = 10;
    note.deleted = false;

    char buf[256];
    size_t n = formatNoteLine(note, buf, sizeof(buf));
    CHECK(n > 0);

    // Parse it back
    Note parsed;
    CHECK(parseNoteLine(buf, n, parsed));
    CHECK(parsed.type == NoteType::Note);
    CHECK_EQ(std::string(parsed.text), "Test note");
}

TEST(color_roundtrip) {
    CHECK_EQ(std::string(colorName(HighlightColor::Yellow)), "yellow");
    CHECK_EQ(std::string(colorName(HighlightColor::Green)), "green");
    CHECK_EQ(std::string(colorName(HighlightColor::Blue)), "blue");
    CHECK_EQ(std::string(colorName(HighlightColor::Pink)), "pink");

    HighlightColor c;
    CHECK(parseColor("yellow", 6, c));
    CHECK(c == HighlightColor::Yellow);
    CHECK(parseColor("green", 5, c));
    CHECK(c == HighlightColor::Green);
    CHECK(parseColor("blue", 4, c));
    CHECK(c == HighlightColor::Blue);
    CHECK(parseColor("pink", 4, c));
    CHECK(c == HighlightColor::Pink);

    CHECK(!parseColor("red", 3, c));
    CHECK(!parseColor("", 0, c));
}
