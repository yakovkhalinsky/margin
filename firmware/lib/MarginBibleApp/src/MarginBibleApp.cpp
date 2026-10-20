// SPDX-License-Identifier: MIT
// STATUS: scaffold. Compiles only inside the PapyriX tree; not yet built or tested on an X4 Pro.
// Provides Bible reading with book/chapter/verse navigation.
// See docs/bible-mode.md for the design.
#include "MarginBibleApp.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <SD.h>

#include <cstdio>
#include <cstring>

#include "ThemeManager.h"
#include "core/Core.h"
#include "margin/bible.h"
#include "ui/Elements.h"

extern GfxRenderer renderer;

namespace papyrix::margin_bible_app {

namespace {

using namespace margin::bible;

constexpr int kMarginX = 16;
constexpr int kMaxLineLen = 128;
constexpr int kMaxVisibleLines = 20;

enum class Mode {
    Reading,      // Normal verse reading
    BookSelect,   // Selecting a book
    ChapterSelect,// Selecting a chapter
    JumpToRef,    // Jump-to-reference input
};

struct ChapterCache {
    uint8_t bookIndex = 0;
    uint16_t chapter = 0;
    char lines[kMaxVisibleLines][kMaxLineLen];
    int lineCount = 0;
    int topLine = 0;  // First visible line (for scrolling)
    bool loaded = false;

    void clear() {
        lineCount = 0;
        topLine = 0;
        loaded = false;
    }
};

struct State {
    NavState nav;
    Mode mode = Mode::Reading;
    ChapterCache cache;
    bool needsRedraw = true;
    int selectorIndex = 0;  // For book/chapter selection
};
State st;

bool loadChapter(uint8_t bookIndex, uint16_t chapter) {
    if (st.cache.loaded && st.cache.bookIndex == bookIndex && st.cache.chapter == chapter) {
        return true;  // Already loaded
    }

    st.cache.clear();
    st.cache.bookIndex = bookIndex;
    st.cache.chapter = chapter;

    const BookInfo* book = findBookByIndex(bookIndex);
    if (!book) return false;

    char path[64];
    snprintf(path, sizeof(path), "/margin/bible/%s.txt", book->id);

    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    // Seek to chapter start
    char line[256];
    bool inChapter = false;
    int targetChapter = chapter;

    while (f.available() && st.cache.lineCount < kMaxVisibleLines) {
        size_t len = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';

        // Trim trailing whitespace
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == ' ')) {
            line[--len] = '\0';
        }

        if (line[0] == '#' && line[1] == '#' && line[2] == '#') {
            // Chapter marker: "### N"
            int ch = 0;
            if (sscanf(line + 3, "%d", &ch) == 1) {
                if (ch == targetChapter) {
                    inChapter = true;
                    continue;  // Skip the marker line itself
                } else if (inChapter) {
                    break;  // Hit next chapter, stop
                }
            }
            continue;
        }

        if (!inChapter) continue;

        // Copy line to cache
        if (st.cache.lineCount < kMaxVisibleLines) {
            strncpy(st.cache.lines[st.cache.lineCount], line, kMaxLineLen - 1);
            st.cache.lines[st.cache.lineCount][kMaxLineLen - 1] = '\0';
            st.cache.lineCount++;
        }
    }

    f.close();
    st.cache.loaded = true;
    return st.cache.lineCount > 0;
}

void renderReading(const Theme& theme) {
    const BookInfo* book = findBookByIndex(st.nav.bookIndex);
    if (!book) return;

    // Title bar: "John 3"
    char title[64];
    snprintf(title, sizeof(title), "%s %d", book->name, st.nav.chapter);
    ui::title(renderer, theme, theme.screenMarginTop, title);

    if (!st.cache.loaded || st.cache.lineCount == 0) {
        renderer.drawCenteredText(theme.uiFontId, renderer.getScreenHeight() / 2,
                                  "No content loaded", theme.secondaryTextBlack);
        return;
    }

    int fontId = theme.uiFontId;
    int y = theme.screenMarginTop + 50;
    int lineHeight = renderer.getLineHeight(fontId);
    int maxY = renderer.getScreenHeight() - 100;

    for (int i = st.cache.topLine; i < st.cache.lineCount && y + lineHeight <= maxY; ++i) {
        const char* line = st.cache.lines[i];
        // Check if this is a verse start (digit at beginning)
        bool isVerseStart = line[0] >= '0' && line[0] <= '9';

        if (isVerseStart) {
            renderer.drawText(fontId, kMarginX, y, line, theme.primaryTextBlack);
        } else {
            // Continuation line (indented in source)
            renderer.drawText(fontId, kMarginX + 20, y, line, theme.primaryTextBlack);
        }
        y += lineHeight;
    }

    // Navigation hint
    ui::ButtonBar buttons("Back", "Jump", "<Book", "Ch>");
    ui::buttonBar(renderer, theme, buttons);
}

void renderBookSelect(const Theme& theme) {
    ui::title(renderer, theme, theme.screenMarginTop, "Select Book");

    int fontId = theme.uiFontId;
    int y = theme.screenMarginTop + 50;
    int lineHeight = renderer.getLineHeight(fontId);
    int maxY = renderer.getScreenHeight() - 100;

    // Show books around the selected one
    int startIdx = st.selectorIndex - 5;
    if (startIdx < 0) startIdx = 0;

    for (size_t i = startIdx; i < kBookCount && y + lineHeight <= maxY; ++i) {
        const BookInfo* book = &kBooks[i];
        bool selected = (i == static_cast<size_t>(st.selectorIndex));

        if (selected) {
            renderer.fillRect(0, y - 2, renderer.getScreenWidth(), lineHeight + 4, theme.primaryTextBlack);
            renderer.drawText(fontId, kMarginX, y, book->name, !theme.primaryTextBlack);
        } else {
            renderer.drawText(fontId, kMarginX, y, book->name, theme.primaryTextBlack);
        }
        y += lineHeight;
    }

    ui::ButtonBar buttons("Cancel", "Select", "Up", "Down");
    ui::buttonBar(renderer, theme, buttons);
}

void renderChapterSelect(const Theme& theme) {
    const BookInfo* book = findBookByIndex(st.nav.bookIndex);
    if (!book) return;

    char title[64];
    snprintf(title, sizeof(title), "%s - Chapter", book->name);
    ui::title(renderer, theme, theme.screenMarginTop, title);

    int fontId = theme.uiFontId;
    int y = theme.screenMarginTop + 50;
    int lineHeight = renderer.getLineHeight(fontId);

    // Show chapter numbers in a grid
    int cols = 5;
    int colWidth = (renderer.getScreenWidth() - 2 * kMarginX) / cols;
    int maxY = renderer.getScreenHeight() - 100;

    for (int ch = 1; ch <= book->chapters && y < maxY; ++ch) {
        int col = (ch - 1) % cols;
        int x = kMarginX + col * colWidth;

        if (col == 0 && ch > 1) {
            y += lineHeight + 4;
            if (y >= maxY) break;
        }

        bool selected = (ch == st.selectorIndex);
        char num[8];
        snprintf(num, sizeof(num), "%d", ch);

        if (selected) {
            int tw = renderer.getTextWidth(fontId, num);
            renderer.fillRect(x - 2, y - 2, tw + 4, lineHeight + 4, theme.primaryTextBlack);
            renderer.drawText(fontId, x, y, num, !theme.primaryTextBlack);
        } else {
            renderer.drawText(fontId, x, y, num, theme.primaryTextBlack);
        }
    }

    ui::ButtonBar buttons("Cancel", "Select", "<", ">");
    ui::buttonBar(renderer, theme, buttons);
}

}  // namespace

void enter(Core& core) {
    (void)core;
    // Initialize with Genesis 1 if not set
    if (st.nav.bookIndex == 0 && st.nav.chapter == 0) {
        st.nav.bookIndex = 0;  // Genesis
        st.nav.chapter = 1;
    }
    st.mode = Mode::Reading;
    st.needsRedraw = true;

    // Try to load the current chapter
    loadChapter(st.nav.bookIndex, st.nav.chapter);
}

bool update(Core& core) {
    (void)core;
    bool redraw = st.needsRedraw;
    st.needsRedraw = false;
    return redraw;
}

void onButton(Core& core, Button btn) {
    (void)core;

    switch (st.mode) {
    case Mode::Reading:
        switch (btn) {
        case Button::Up:
            if (st.cache.topLine > 0) {
                st.cache.topLine--;
                st.needsRedraw = true;
            }
            break;
        case Button::Down:
            if (st.cache.topLine < st.cache.lineCount - 5) {
                st.cache.topLine++;
                st.needsRedraw = true;
            }
            break;
        case Button::Left:  // Previous book
            st.mode = Mode::BookSelect;
            st.selectorIndex = st.nav.bookIndex;
            st.needsRedraw = true;
            break;
        case Button::Right: // Next chapter
            st.mode = Mode::ChapterSelect;
            st.selectorIndex = st.nav.chapter;
            st.needsRedraw = true;
            break;
        case Button::Center: // Jump to reference
            // TODO(M1): Implement jump-to-reference input
            break;
        default:
            break;
        }
        break;

    case Mode::BookSelect:
        switch (btn) {
        case Button::Up:
            if (st.selectorIndex > 0) st.selectorIndex--;
            st.needsRedraw = true;
            break;
        case Button::Down:
            if (st.selectorIndex < static_cast<int>(kBookCount) - 1) st.selectorIndex++;
            st.needsRedraw = true;
            break;
        case Button::Center:
            st.nav.bookIndex = st.selectorIndex;
            st.nav.chapter = 1;
            st.cache.clear();
            loadChapter(st.nav.bookIndex, st.nav.chapter);
            st.mode = Mode::Reading;
            st.needsRedraw = true;
            break;
        case Button::Back:
            st.mode = Mode::Reading;
            st.needsRedraw = true;
            break;
        default:
            break;
        }
        break;

    case Mode::ChapterSelect: {
        const BookInfo* book = findBookByIndex(st.nav.bookIndex);
        if (!book) break;
        switch (btn) {
        case Button::Left:
            if (st.selectorIndex > 1) st.selectorIndex--;
            st.needsRedraw = true;
            break;
        case Button::Right:
            if (st.selectorIndex < book->chapters) st.selectorIndex++;
            st.needsRedraw = true;
            break;
        case Button::Up:
            if (st.selectorIndex > 5) st.selectorIndex -= 5;
            else st.selectorIndex = 1;
            st.needsRedraw = true;
            break;
        case Button::Down:
            st.selectorIndex += 5;
            if (st.selectorIndex > book->chapters) st.selectorIndex = book->chapters;
            st.needsRedraw = true;
            break;
        case Button::Center:
            st.nav.chapter = st.selectorIndex;
            st.cache.clear();
            loadChapter(st.nav.bookIndex, st.nav.chapter);
            st.mode = Mode::Reading;
            st.needsRedraw = true;
            break;
        case Button::Back:
            st.mode = Mode::Reading;
            st.needsRedraw = true;
            break;
        default:
            break;
        }
        break;
    }

    case Mode::JumpToRef:
        // TODO: Implement reference input
        if (btn == Button::Back) {
            st.mode = Mode::Reading;
            st.needsRedraw = true;
        }
        break;
    }
}

bool render(Core& core) {
    (void)core;
    const Theme& theme = THEME;
    renderer.clearScreen(theme.backgroundColor);

    switch (st.mode) {
    case Mode::Reading:
        renderReading(theme);
        break;
    case Mode::BookSelect:
        renderBookSelect(theme);
        break;
    case Mode::ChapterSelect:
        renderChapterSelect(theme);
        break;
    case Mode::JumpToRef:
        // TODO: Render reference input screen
        ui::title(renderer, theme, theme.screenMarginTop, "Jump to Reference");
        renderer.drawCenteredText(theme.uiFontId, renderer.getScreenHeight() / 2,
                                  "Not implemented yet", theme.secondaryTextBlack);
        break;
    }

    return false;  // Let AppLauncherState push the frame
}

void exit(Core& core) {
    (void)core;
    // Save navigation state for resume
    // TODO: Persist to SD card or NVS
}

}  // namespace papyrix::margin_bible_app
