// SPDX-License-Identifier: MIT
#include "margin/bible.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace margin::bible {

namespace {

// Skip whitespace, return new position
const char* skipWs(const char* s, const char* end) {
    while (s < end && std::isspace(static_cast<unsigned char>(*s))) ++s;
    return s;
}

// Parse an unsigned integer, return new position (or nullptr on failure)
const char* parseUint(const char* s, const char* end, uint16_t& out) {
    if (s >= end || !std::isdigit(static_cast<unsigned char>(*s))) return nullptr;
    uint32_t val = 0;
    while (s < end && std::isdigit(static_cast<unsigned char>(*s))) {
        val = val * 10 + (*s - '0');
        if (val > 65535) return nullptr;
        ++s;
    }
    out = static_cast<uint16_t>(val);
    return s;
}

// Case-insensitive prefix match
bool ciPrefix(const char* s, size_t sLen, const char* prefix) {
    size_t pLen = std::strlen(prefix);
    if (sLen < pLen) return false;
    for (size_t i = 0; i < pLen; ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

// Find a JSON string value for a key in a simple JSON object
// Returns pointer to the value start (after opening quote), and sets valueLen
const char* findJsonString(const char* json, size_t jsonLen, const char* key, size_t& valueLen) {
    char pattern[64];
    int n = std::snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(pattern)) return nullptr;

    const char* pos = std::strstr(json, pattern);
    if (!pos || pos >= json + jsonLen) return nullptr;

    pos += n;
    while (pos < json + jsonLen && std::isspace(static_cast<unsigned char>(*pos))) ++pos;
    if (pos >= json + jsonLen || *pos != '"') return nullptr;
    ++pos;

    const char* end = pos;
    while (end < json + jsonLen && *end != '"') {
        if (*end == '\\' && end + 1 < json + jsonLen) ++end;
        ++end;
    }
    valueLen = end - pos;
    return pos;
}

// Find a JSON number value for a key
bool findJsonNumber(const char* json, size_t jsonLen, const char* key, uint32_t& out) {
    char pattern[64];
    int n = std::snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(pattern)) return false;

    const char* pos = std::strstr(json, pattern);
    if (!pos || pos >= json + jsonLen) return false;

    pos += n;
    while (pos < json + jsonLen && std::isspace(static_cast<unsigned char>(*pos))) ++pos;

    uint32_t val = 0;
    while (pos < json + jsonLen && std::isdigit(static_cast<unsigned char>(*pos))) {
        val = val * 10 + (*pos - '0');
        ++pos;
    }
    out = val;
    return true;
}

// Find a JSON boolean value for a key
bool findJsonBool(const char* json, size_t jsonLen, const char* key, bool& out) {
    char pattern[64];
    int n = std::snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(pattern)) return false;

    const char* pos = std::strstr(json, pattern);
    if (!pos || pos >= json + jsonLen) return false;

    pos += n;
    while (pos < json + jsonLen && std::isspace(static_cast<unsigned char>(*pos))) ++pos;

    if (pos + 4 <= json + jsonLen && std::strncmp(pos, "true", 4) == 0) {
        out = true;
        return true;
    }
    if (pos + 5 <= json + jsonLen && std::strncmp(pos, "false", 5) == 0) {
        out = false;
        return true;
    }
    return false;
}

}  // namespace

// Book data: OSIS ID, full name, common abbreviation, chapter count, is NT
const BookInfo kBooks[66] = {
    {"GEN", "Genesis", "Gen", 50, false},
    {"EXO", "Exodus", "Ex", 40, false},
    {"LEV", "Leviticus", "Lev", 27, false},
    {"NUM", "Numbers", "Num", 36, false},
    {"DEU", "Deuteronomy", "Deut", 34, false},
    {"JOS", "Joshua", "Josh", 24, false},
    {"JDG", "Judges", "Judg", 21, false},
    {"RUT", "Ruth", "Ruth", 4, false},
    {"1SA", "1 Samuel", "1Sam", 31, false},
    {"2SA", "2 Samuel", "2Sam", 24, false},
    {"1KI", "1 Kings", "1Kgs", 22, false},
    {"2KI", "2 Kings", "2Kgs", 25, false},
    {"1CH", "1 Chronicles", "1Chr", 29, false},
    {"2CH", "2 Chronicles", "2Chr", 36, false},
    {"EZR", "Ezra", "Ezra", 10, false},
    {"NEH", "Nehemiah", "Neh", 13, false},
    {"EST", "Esther", "Esth", 10, false},
    {"JOB", "Job", "Job", 42, false},
    {"PSA", "Psalms", "Ps", 150, false},
    {"PRO", "Proverbs", "Prov", 31, false},
    {"ECC", "Ecclesiastes", "Eccl", 12, false},
    {"SNG", "Song of Solomon", "Song", 8, false},
    {"ISA", "Isaiah", "Isa", 66, false},
    {"JER", "Jeremiah", "Jer", 52, false},
    {"LAM", "Lamentations", "Lam", 5, false},
    {"EZK", "Ezekiel", "Ezek", 48, false},
    {"DAN", "Daniel", "Dan", 12, false},
    {"HOS", "Hosea", "Hos", 14, false},
    {"JOL", "Joel", "Joel", 3, false},
    {"AMO", "Amos", "Amos", 9, false},
    {"OBA", "Obadiah", "Obad", 1, false},
    {"JON", "Jonah", "Jonah", 4, false},
    {"MIC", "Micah", "Mic", 7, false},
    {"NAH", "Nahum", "Nah", 3, false},
    {"HAB", "Habakkuk", "Hab", 3, false},
    {"ZEP", "Zephaniah", "Zeph", 3, false},
    {"HAG", "Haggai", "Hag", 2, false},
    {"ZEC", "Zechariah", "Zech", 14, false},
    {"MAL", "Malachi", "Mal", 4, false},
    {"MAT", "Matthew", "Matt", 28, true},
    {"MRK", "Mark", "Mark", 16, true},
    {"LUK", "Luke", "Luke", 24, true},
    {"JHN", "John", "John", 21, true},
    {"ACT", "Acts", "Acts", 28, true},
    {"ROM", "Romans", "Rom", 16, true},
    {"1CO", "1 Corinthians", "1Cor", 16, true},
    {"2CO", "2 Corinthians", "2Cor", 13, true},
    {"GAL", "Galatians", "Gal", 6, true},
    {"EPH", "Ephesians", "Eph", 6, true},
    {"PHP", "Philippians", "Phil", 4, true},
    {"COL", "Colossians", "Col", 4, true},
    {"1TH", "1 Thessalonians", "1Thess", 5, true},
    {"2TH", "2 Thessalonians", "2Thess", 3, true},
    {"1TI", "1 Timothy", "1Tim", 6, true},
    {"2TI", "2 Timothy", "2Tim", 4, true},
    {"TIT", "Titus", "Titus", 3, true},
    {"PHM", "Philemon", "Phlm", 1, true},
    {"HEB", "Hebrews", "Heb", 13, true},
    {"JAS", "James", "Jas", 5, true},
    {"1PE", "1 Peter", "1Pet", 5, true},
    {"2PE", "2 Peter", "2Pet", 3, true},
    {"1JN", "1 John", "1John", 5, true},
    {"2JN", "2 John", "2John", 1, true},
    {"3JN", "3 John", "3John", 1, true},
    {"JUD", "Jude", "Jude", 1, true},
    {"REV", "Revelation", "Rev", 22, true},
};

const BookInfo* findBook(const char* id) {
    if (!id) return nullptr;
    for (size_t i = 0; i < kBookCount; ++i) {
        if (std::strcmp(kBooks[i].id, id) == 0) return &kBooks[i];
    }
    return nullptr;
}

const BookInfo* findBookByIndex(size_t index) {
    if (index >= kBookCount) return nullptr;
    return &kBooks[index];
}

int bookIndex(const char* id) {
    if (!id) return -1;
    for (size_t i = 0; i < kBookCount; ++i) {
        if (std::strcmp(kBooks[i].id, id) == 0) return static_cast<int>(i);
    }
    return -1;
}

const BookInfo* findBookByName(const char* name, size_t len) {
    if (!name || len == 0) return nullptr;

    // Try exact match on ID first (case-insensitive)
    if (len == 3) {
        char upper[4];
        for (size_t i = 0; i < 3; ++i) {
            upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
        }
        upper[3] = '\0';
        const BookInfo* b = findBook(upper);
        if (b) return b;
    }

    // Handle numbered books: "1 John", "1John", "I John"
    size_t start = 0;
    int bookNum = 0;
    if (len > 0 && (name[0] == '1' || name[0] == '2' || name[0] == '3' ||
                    name[0] == 'I' || name[0] == 'i')) {
        if (name[0] == 'I' || name[0] == 'i') {
            // Roman numeral: I, II, III
            size_t numEnd = 1;
            while (numEnd < len && (name[numEnd] == 'I' || name[numEnd] == 'i')) ++numEnd;
            bookNum = static_cast<int>(numEnd);
            start = numEnd;
        } else {
            bookNum = name[0] - '0';
            start = 1;
        }
        // Skip optional space after number
        if (start < len && std::isspace(static_cast<unsigned char>(name[start]))) ++start;
    }

    // Try matching against book names and abbreviations
    for (size_t i = 0; i < kBookCount; ++i) {
        const BookInfo& b = kBooks[i];

        // Check if this book expects a number prefix
        bool hasNum = (b.name[0] == '1' || b.name[0] == '2' || b.name[0] == '3');
        if (hasNum && bookNum > 0) {
            int expectedNum = b.name[0] - '0';
            if (expectedNum != bookNum) continue;
            // Match the rest of the name (after "N ")
            const char* restName = b.name + 2;
            const char* restAbbrev = b.abbrev + 1;  // "1Sam" -> "Sam"
            if (ciPrefix(name + start, len - start, restName)) return &b;
            if (ciPrefix(name + start, len - start, restAbbrev)) return &b;
        } else if (!hasNum && bookNum == 0) {
            // Non-numbered book
            if (ciPrefix(name, len, b.name)) return &b;
            if (ciPrefix(name, len, b.abbrev)) return &b;
            if (ciPrefix(name, len, b.id)) return &b;
        }
    }

    // Special cases for common variants
    static const struct { const char* variant; const char* id; } variants[] = {
        {"Jn", "JHN"}, {"Joh", "JHN"}, {"Jno", "JHN"},
        {"Gn", "GEN"}, {"Ge", "GEN"},
        {"Ex", "EXO"}, {"Exod", "EXO"},
        {"Lv", "LEV"}, {"Le", "LEV"},
        {"Nm", "NUM"}, {"Nu", "NUM"},
        {"Dt", "DEU"}, {"De", "DEU"},
        {"Jos", "JOS"}, {"Jo", "JOS"},
        {"Jg", "JDG"}, {"Jdg", "JDG"}, {"Jud", "JDG"},
        {"Ru", "RUT"}, {"Rt", "RUT"},
        {"Ezr", "EZR"}, {"Ez", "EZR"},
        {"Ne", "NEH"},
        {"Es", "EST"}, {"Esth", "EST"},
        {"Jb", "JOB"},
        {"Psa", "PSA"}, {"Psalm", "PSA"}, {"Pss", "PSA"},
        {"Pr", "PRO"}, {"Pro", "PRO"},
        {"Ec", "ECC"}, {"Qoh", "ECC"},
        {"So", "SNG"}, {"Sg", "SNG"}, {"Cant", "SNG"}, {"SS", "SNG"},
        {"Is", "ISA"},
        {"Je", "JER"}, {"Jr", "JER"},
        {"La", "LAM"},
        {"Eze", "EZK"}, {"Ez", "EZK"},
        {"Da", "DAN"}, {"Dn", "DAN"},
        {"Ho", "HOS"},
        {"Jl", "JOL"}, {"Joe", "JOL"},
        {"Am", "AMO"},
        {"Ob", "OBA"},
        {"Jon", "JON"}, {"Jnh", "JON"},
        {"Mi", "MIC"},
        {"Na", "NAH"},
        {"Hb", "HAB"},
        {"Zp", "ZEP"}, {"Zph", "ZEP"},
        {"Hg", "HAG"},
        {"Zc", "ZEC"}, {"Zch", "ZEC"},
        {"Ml", "MAL"},
        {"Mt", "MAT"}, {"Mat", "MAT"},
        {"Mk", "MRK"}, {"Mr", "MRK"}, {"Mar", "MRK"},
        {"Lk", "LUK"}, {"Lu", "LUK"},
        {"Ac", "ACT"},
        {"Ro", "ROM"}, {"Rm", "ROM"},
        {"Ga", "GAL"},
        {"Php", "PHP"}, {"Phi", "PHP"},
        {"Hbr", "HEB"}, {"He", "HEB"},
        {"Jm", "JAS"}, {"Jam", "JAS"},
        {"Rv", "REV"}, {"Re", "REV"}, {"Apoc", "REV"},
    };
    for (const auto& v : variants) {
        if (ciPrefix(name, len, v.variant) && std::strlen(v.variant) == len) {
            return findBook(v.id);
        }
    }

    return nullptr;
}

bool Reference::isValid() const {
    const BookInfo* b = findBook(book);
    if (!b) return false;
    if (chapter < 1 || chapter > b->chapters) return false;
    return true;
}

bool parseReference(const char* input, size_t len, Reference& out) {
    if (!input || len == 0) return false;

    const char* end = input + len;
    const char* p = skipWs(input, end);

    // Check for OSIS format: "GEN.1.1" or "GEN.1.1-3"
    const char* dot = static_cast<const char*>(std::memchr(p, '.', end - p));
    if (dot && dot - p == 3) {
        // Likely OSIS format
        char bookId[4];
        for (int i = 0; i < 3; ++i) {
            bookId[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(p[i])));
        }
        bookId[3] = '\0';

        const BookInfo* b = findBook(bookId);
        if (b) {
            p = dot + 1;
            uint16_t chapter;
            p = parseUint(p, end, chapter);
            if (!p) return false;

            std::memcpy(out.book, bookId, 4);
            out.chapter = chapter;
            out.verse = 0;
            out.endVerse = 0;

            if (p < end && *p == '.') {
                ++p;
                p = parseUint(p, end, out.verse);
                if (!p) return false;

                if (p < end && *p == '-') {
                    ++p;
                    p = parseUint(p, end, out.endVerse);
                }
            }
            return out.isValid();
        }
    }

    // Parse book name (may include leading number for "1 John" etc.)
    const char* bookStart = p;
    const char* bookEnd = p;

    // Handle leading number: "1 John", "2 Samuel"
    if (p < end && (*p == '1' || *p == '2' || *p == '3')) {
        ++p;
        if (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
    }

    // Read until we hit a digit (chapter number) or separator
    while (p < end && !std::isdigit(static_cast<unsigned char>(*p)) &&
           *p != ':' && *p != '.' && *p != ' ') {
        ++p;
    }
    bookEnd = p;

    // Skip trailing space before chapter
    while (bookEnd > bookStart && std::isspace(static_cast<unsigned char>(*(bookEnd - 1)))) {
        --bookEnd;
    }

    // Handle case where there's a space before chapter: "John 3:16"
    p = skipWs(p, end);

    const BookInfo* b = findBookByName(bookStart, bookEnd - bookStart);
    if (!b) return false;

    std::memcpy(out.book, b->id, 4);

    // Parse chapter
    if (p >= end || !std::isdigit(static_cast<unsigned char>(*p))) {
        out.chapter = 1;
        out.verse = 0;
        out.endVerse = 0;
        return true;
    }

    p = parseUint(p, end, out.chapter);
    if (!p) return false;

    out.verse = 0;
    out.endVerse = 0;

    // Parse verse if present
    p = skipWs(p, end);
    if (p < end && (*p == ':' || *p == '.')) {
        ++p;
        p = skipWs(p, end);
        p = parseUint(p, end, out.verse);
        if (!p) return false;

        // Parse verse range
        p = skipWs(p, end);
        if (p < end && *p == '-') {
            ++p;
            p = skipWs(p, end);
            p = parseUint(p, end, out.endVerse);
        }
    }

    return out.isValid();
}

size_t formatReference(const Reference& ref, char* buf, size_t cap) {
    const BookInfo* b = findBook(ref.book);
    if (!b || !buf || cap == 0) return 0;

    int n;
    if (ref.verse == 0) {
        n = std::snprintf(buf, cap, "%s %u", b->name, ref.chapter);
    } else if (ref.endVerse > 0) {
        n = std::snprintf(buf, cap, "%s %u:%u-%u", b->name, ref.chapter, ref.verse, ref.endVerse);
    } else {
        n = std::snprintf(buf, cap, "%s %u:%u", b->name, ref.chapter, ref.verse);
    }

    if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
    return static_cast<size_t>(n);
}

size_t formatOsis(const Reference& ref, char* buf, size_t cap) {
    if (!buf || cap == 0) return 0;

    int n;
    if (ref.verse == 0) {
        n = std::snprintf(buf, cap, "%s.%u", ref.book, ref.chapter);
    } else if (ref.endVerse > 0) {
        n = std::snprintf(buf, cap, "%s.%u.%u-%u", ref.book, ref.chapter, ref.verse, ref.endVerse);
    } else {
        n = std::snprintf(buf, cap, "%s.%u.%u", ref.book, ref.chapter, ref.verse);
    }

    if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
    return static_cast<size_t>(n);
}

const char* colorName(HighlightColor c) {
    switch (c) {
        case HighlightColor::Yellow: return "yellow";
        case HighlightColor::Green: return "green";
        case HighlightColor::Blue: return "blue";
        case HighlightColor::Pink: return "pink";
    }
    return "yellow";
}

bool parseColor(const char* name, size_t len, HighlightColor& out) {
    if (!name || len == 0) return false;
    if (ciPrefix(name, len, "yellow") && len == 6) { out = HighlightColor::Yellow; return true; }
    if (ciPrefix(name, len, "green") && len == 5) { out = HighlightColor::Green; return true; }
    if (ciPrefix(name, len, "blue") && len == 4) { out = HighlightColor::Blue; return true; }
    if (ciPrefix(name, len, "pink") && len == 4) { out = HighlightColor::Pink; return true; }
    return false;
}

bool parseNoteLine(const char* line, size_t len, Note& out) {
    if (!line || len == 0) return false;

    out = Note{};

    // Parse "ref" field
    size_t refLen;
    const char* refVal = findJsonString(line, len, "ref", refLen);
    if (!refVal || !parseReference(refVal, refLen, out.ref)) return false;

    // Parse "type" field
    size_t typeLen;
    const char* typeVal = findJsonString(line, len, "type", typeLen);
    if (!typeVal) return false;
    if (ciPrefix(typeVal, typeLen, "highlight")) {
        out.type = NoteType::Highlight;
    } else if (ciPrefix(typeVal, typeLen, "note")) {
        out.type = NoteType::Note;
    } else {
        return false;
    }

    // Parse "color" field (optional, for highlights)
    size_t colorLen;
    const char* colorVal = findJsonString(line, len, "color", colorLen);
    if (colorVal) {
        parseColor(colorVal, colorLen, out.color);
    }

    // Parse "text" field (optional, for notes)
    size_t textLen;
    const char* textVal = findJsonString(line, len, "text", textLen);
    if (textVal && textLen > 0) {
        size_t copyLen = textLen < sizeof(out.text) - 1 ? textLen : sizeof(out.text) - 1;
        std::memcpy(out.text, textVal, copyLen);
        out.text[copyLen] = '\0';
    }

    // Parse timestamp and sequence
    uint32_t ts = 0, seq = 0;
    findJsonNumber(line, len, "ts", ts);
    findJsonNumber(line, len, "seq", seq);
    out.timestamp = ts;
    out.seq = seq;

    // Parse deleted flag
    findJsonBool(line, len, "deleted", out.deleted);

    return true;
}

size_t formatNoteLine(const Note& note, char* buf, size_t cap) {
    if (!buf || cap == 0) return 0;

    char osisBuf[32];
    size_t osisLen = formatOsis(note.ref, osisBuf, sizeof(osisBuf));
    if (osisLen == 0) return 0;

    const char* typeName = note.type == NoteType::Highlight ? "highlight" : "note";

    int n;
    if (note.type == NoteType::Highlight) {
        if (note.deleted) {
            n = std::snprintf(buf, cap, R"({"ref":"%s","type":"%s","color":"%s","ts":%u,"seq":%u,"deleted":true})",
                              osisBuf, typeName, colorName(note.color), note.timestamp, note.seq);
        } else {
            n = std::snprintf(buf, cap, R"({"ref":"%s","type":"%s","color":"%s","ts":%u,"seq":%u})",
                              osisBuf, typeName, colorName(note.color), note.timestamp, note.seq);
        }
    } else {
        // Escape text for JSON (simple: just escape quotes and backslashes)
        char escaped[512];
        size_t ei = 0;
        for (const char* p = note.text; *p && ei < sizeof(escaped) - 2; ++p) {
            if (*p == '"' || *p == '\\') {
                if (ei + 2 >= sizeof(escaped)) break;
                escaped[ei++] = '\\';
            }
            escaped[ei++] = *p;
        }
        escaped[ei] = '\0';

        if (note.deleted) {
            n = std::snprintf(buf, cap, R"({"ref":"%s","type":"%s","text":"%s","ts":%u,"seq":%u,"deleted":true})",
                              osisBuf, typeName, escaped, note.timestamp, note.seq);
        } else {
            n = std::snprintf(buf, cap, R"({"ref":"%s","type":"%s","text":"%s","ts":%u,"seq":%u})",
                              osisBuf, typeName, escaped, note.timestamp, note.seq);
        }
    }

    if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
    return static_cast<size_t>(n);
}

}  // namespace margin::bible
