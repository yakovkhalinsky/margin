// SPDX-License-Identifier: MIT
// STATUS: scaffold. Compiles for CrossPoint on X4 Pro; not yet tested on hardware.
// Provides structured Bible reading with book/chapter/verse navigation.
// See docs/bible-mode.md for the design.
#include "BibleActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Preferences.h>

#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace margin {

using namespace bible;

static constexpr const char* TAG = "BIBLE";
static constexpr const char* PREFS_NS = "margin_bible";
static constexpr const char* BIBLE_PATH = "/margin/bible";
static constexpr int MAX_VISIBLE_LINES = 20;

BibleActivity::BibleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Bible", renderer, mappedInput) {}

void BibleActivity::onEnter() {
    Activity::onEnter();
    loadPosition();

    // Initialize to Genesis 1 if not set
    if (nav_.bookIndex == 0 && nav_.chapter == 0) {
        nav_.bookIndex = 0;
        nav_.chapter = 1;
    }

    mode_ = Mode::Reading;
    loadChapter(nav_.bookIndex, nav_.chapter);
    requestUpdateAndWait();
}

void BibleActivity::onExit() {
    savePosition();
    Activity::onExit();
}

void BibleActivity::loop() {
    switch (mode_) {
        case Mode::Reading:
            handleReadingInput();
            break;
        case Mode::BookSelect:
            handleBookSelectInput();
            break;
        case Mode::ChapterSelect:
            handleChapterSelectInput();
            break;
        case Mode::JumpToRef:
            handleJumpToRefInput();
            break;
    }
}

void BibleActivity::handleReadingInput() {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        finish();
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (cache_.topLine > 0) {
            cache_.topLine--;
            requestUpdate();
        }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        if (cache_.topLine < static_cast<int>(cache_.lines.size()) - 5) {
            cache_.topLine++;
            requestUpdate();
        }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
        // Previous chapter
        if (nav_.chapter > 1) {
            nav_.chapter--;
            loadChapter(nav_.bookIndex, nav_.chapter);
            requestUpdate();
        } else if (nav_.bookIndex > 0) {
            nav_.bookIndex--;
            const auto* book = findBookByIndex(nav_.bookIndex);
            if (book) {
                nav_.chapter = book->chapters;
                loadChapter(nav_.bookIndex, nav_.chapter);
                requestUpdate();
            }
        }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
        // Next chapter
        const auto* book = findBookByIndex(nav_.bookIndex);
        if (book && nav_.chapter < book->chapters) {
            nav_.chapter++;
            loadChapter(nav_.bookIndex, nav_.chapter);
            requestUpdate();
        } else if (nav_.bookIndex < kBookCount - 1) {
            nav_.bookIndex++;
            nav_.chapter = 1;
            loadChapter(nav_.bookIndex, nav_.chapter);
            requestUpdate();
        }
    }

    // Center/confirm button: open book selector
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        mode_ = Mode::BookSelect;
        selectorIndex_ = nav_.bookIndex;
        requestUpdate();
    }

    // Touch: tap top area for book/chapter selector
    int x = 0, y = 0;
    if (mappedInput.wasScreenTapped(x, y)) {
        const auto& metrics = UITheme::getInstance().getMetrics();
        if (y < metrics.headerHeight + metrics.topPadding) {
            mode_ = Mode::BookSelect;
            selectorIndex_ = nav_.bookIndex;
            requestUpdate();
        }
    }
}

void BibleActivity::handleBookSelectInput() {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        mode_ = Mode::Reading;
        requestUpdate();
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (selectorIndex_ > 0) {
            selectorIndex_--;
            requestUpdate();
        }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        if (selectorIndex_ < static_cast<int>(kBookCount) - 1) {
            selectorIndex_++;
            requestUpdate();
        }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
        // Select book, go to chapter selector
        nav_.bookIndex = selectorIndex_;
        mode_ = Mode::ChapterSelect;
        selectorIndex_ = nav_.chapter;
        requestUpdate();
    }

    // Touch: tap a book to select
    int x = 0, y = 0;
    if (mappedInput.wasScreenTapped(x, y)) {
        const auto& metrics = UITheme::getInstance().getMetrics();
        const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 4;
        const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
        const int tappedIndex = (y - startY) / lineHeight;

        if (tappedIndex >= 0 && tappedIndex < static_cast<int>(kBookCount)) {
            // Adjust for scroll offset
            int firstVisible = selectorIndex_ - 5;
            if (firstVisible < 0) firstVisible = 0;
            const int actualIndex = firstVisible + tappedIndex;
            if (actualIndex >= 0 && actualIndex < static_cast<int>(kBookCount)) {
                nav_.bookIndex = actualIndex;
                mode_ = Mode::ChapterSelect;
                selectorIndex_ = 1;
                requestUpdate();
            }
        }
    }
}

void BibleActivity::handleChapterSelectInput() {
    const auto* book = findBookByIndex(nav_.bookIndex);
    if (!book) {
        mode_ = Mode::Reading;
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        mode_ = Mode::BookSelect;
        selectorIndex_ = nav_.bookIndex;
        requestUpdate();
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (selectorIndex_ > 5) {
            selectorIndex_ -= 5;
        } else {
            selectorIndex_ = 1;
        }
        requestUpdate();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        selectorIndex_ += 5;
        if (selectorIndex_ > book->chapters) {
            selectorIndex_ = book->chapters;
        }
        requestUpdate();
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
        if (selectorIndex_ > 1) {
            selectorIndex_--;
            requestUpdate();
        }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
        if (selectorIndex_ < book->chapters) {
            selectorIndex_++;
            requestUpdate();
        }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        nav_.chapter = selectorIndex_;
        cache_.clear();
        loadChapter(nav_.bookIndex, nav_.chapter);
        mode_ = Mode::Reading;
        requestUpdate();
    }

    // Touch: tap a chapter number
    int x = 0, y = 0;
    if (mappedInput.wasScreenTapped(x, y)) {
        const auto& metrics = UITheme::getInstance().getMetrics();
        const int cols = 5;
        const int colWidth = (renderer.getScreenWidth() - 2 * metrics.contentSidePadding) / cols;
        const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 8;
        const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
        const int startX = metrics.contentSidePadding;

        const int col = (x - startX) / colWidth;
        const int row = (y - startY) / lineHeight;
        const int chapter = row * cols + col + 1;

        if (chapter >= 1 && chapter <= book->chapters) {
            nav_.chapter = chapter;
            cache_.clear();
            loadChapter(nav_.bookIndex, nav_.chapter);
            mode_ = Mode::Reading;
            requestUpdate();
        }
    }
}

void BibleActivity::handleJumpToRefInput() {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        mode_ = Mode::Reading;
        refInput_.clear();
        requestUpdate();
        return;
    }

    // TODO: Implement keyboard input for reference
    // For now, just go back to reading
    mode_ = Mode::Reading;
    requestUpdate();
}

bool BibleActivity::loadChapter(uint8_t bookIndex, uint16_t chapter) {
    if (cache_.loaded && cache_.bookIndex == bookIndex && cache_.chapter == chapter) {
        return true;
    }

    cache_.clear();
    cache_.bookIndex = bookIndex;
    cache_.chapter = chapter;

    const auto* book = findBookByIndex(bookIndex);
    if (!book) {
        LOG_ERR(TAG, "Invalid book index: %d", bookIndex);
        return false;
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/%s.txt", BIBLE_PATH, book->id);

    HalFile file = Storage.open(path, O_RDONLY);
    if (!file) {
        LOG_ERR(TAG, "Failed to open %s", path);
        return false;
    }

    bool inChapter = false;
    std::string currentLine;
    currentLine.reserve(256);

    while (cache_.lines.size() < MAX_VISIBLE_LINES * 3) {
        int ch = file.read();
        if (ch < 0) break;  // EOF or error

        if (ch == '\n' || ch == '\r') {
            // Handle \r\n by skipping the second newline char
            if (ch == '\r') {
                int next = file.read();
                if (next >= 0 && next != '\n') {
                    // Put it back by seeking (not a \r\n pair)
                    file.seekCur(-1);
                }
            }

            // Process the completed line
            if (currentLine.size() >= 3 && currentLine[0] == '#' && 
                currentLine[1] == '#' && currentLine[2] == '#') {
                int parsedChapter = 0;
                if (sscanf(currentLine.c_str() + 3, "%d", &parsedChapter) == 1) {
                    if (parsedChapter == static_cast<int>(chapter)) {
                        inChapter = true;
                    } else if (inChapter) {
                        break;  // Past our chapter
                    }
                }
            } else if (inChapter && !currentLine.empty()) {
                cache_.lines.push_back(currentLine);
            }
            currentLine.clear();
        } else {
            if (currentLine.size() < 255) {
                currentLine += static_cast<char>(ch);
            }
        }
    }

    // Don't forget the last line if file doesn't end with newline
    if (inChapter && !currentLine.empty()) {
        cache_.lines.push_back(currentLine);
    }

    file.close();
    cache_.loaded = true;
    cache_.topLine = 0;

    LOG_INF(TAG, "Loaded %s chapter %d: %zu lines", book->name, chapter, cache_.lines.size());
    return !cache_.lines.empty();
}

void BibleActivity::savePosition() {
    Preferences prefs;
    if (prefs.begin(PREFS_NS, false)) {
        prefs.putUChar("book", nav_.bookIndex);
        prefs.putUShort("chapter", nav_.chapter);
        prefs.putUShort("verse", nav_.topVerse);
        prefs.end();
    }
}

void BibleActivity::loadPosition() {
    Preferences prefs;
    if (prefs.begin(PREFS_NS, true)) {
        nav_.bookIndex = prefs.getUChar("book", 0);
        nav_.chapter = prefs.getUShort("chapter", 1);
        nav_.topVerse = prefs.getUShort("verse", 0);
        prefs.end();
    }
}

void BibleActivity::render(RenderLock&&) {
    renderer.clearScreen();

    switch (mode_) {
        case Mode::Reading:
            renderReading();
            break;
        case Mode::BookSelect:
            renderBookSelect();
            break;
        case Mode::ChapterSelect:
            renderChapterSelect();
            break;
        case Mode::JumpToRef:
            renderJumpToRef();
            break;
    }

    renderer.displayBuffer();
}

void BibleActivity::renderReading() {
    const auto* book = findBookByIndex(nav_.bookIndex);
    if (!book) return;

    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const int contentWidth = pageWidth - 2 * metrics.contentSidePadding;
    const int x = metrics.contentSidePadding;

    // Header with book and chapter
    char title[64];
    snprintf(title, sizeof(title), "%s %d", book->name, nav_.chapter);
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title, nullptr, true);

    if (!cache_.loaded || cache_.lines.empty()) {
        const int centerY = renderer.getScreenHeight() / 2;
        renderer.drawCenteredText(UI_10_FONT_ID, centerY, "No Bible data found");
        renderer.drawCenteredText(UI_10_FONT_ID, centerY + 30, "Run: python tools/bsb-converter.py");
        renderer.drawCenteredText(UI_10_FONT_ID, centerY + 60, "Copy /margin/bible/ to SD card");
        return;
    }

    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int maxY = renderer.getScreenHeight() - metrics.buttonHintsHeight - 10;

    for (size_t i = cache_.topLine; i < cache_.lines.size() && y + lineHeight <= maxY; ++i) {
        const auto& line = cache_.lines[i];

        // Check if this is a verse start (digit at beginning)
        bool isVerseStart = !line.empty() && line[0] >= '0' && line[0] <= '9';

        if (isVerseStart) {
            renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
        } else {
            // Continuation line (indented in source)
            renderer.drawText(UI_10_FONT_ID, x + 20, y, line.c_str());
        }
        y += lineHeight;
    }

    // Button hints
    const auto labels = mappedInput.mapLabels("Back", "Select", "Prev", "Next");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BibleActivity::renderBookSelect() {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const int x = metrics.contentSidePadding;

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Select Book", nullptr,
                   true);

    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 4;
    const int maxY = renderer.getScreenHeight() - metrics.buttonHintsHeight - 10;

    // Show books around the selected one
    int startIdx = selectorIndex_ - 5;
    if (startIdx < 0) startIdx = 0;

    for (size_t i = startIdx; i < kBookCount && y + lineHeight <= maxY; ++i) {
        const auto* book = &kBooks[i];
        const bool selected = (i == static_cast<size_t>(selectorIndex_));

        if (selected) {
            renderer.fillRect(0, y - 2, pageWidth, lineHeight, true);
            renderer.drawText(UI_10_FONT_ID, x, y, book->name, false);
        } else {
            renderer.drawText(UI_10_FONT_ID, x, y, book->name, true);
        }

        // Show if OT or NT
        const char* testament = book->isNT ? "NT" : "OT";
        renderer.drawText(UI_10_FONT_ID, pageWidth - metrics.contentSidePadding - 30, y, testament);

        y += lineHeight;
    }

    const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BibleActivity::renderChapterSelect() {
    const auto* book = findBookByIndex(nav_.bookIndex);
    if (!book) return;

    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const int x = metrics.contentSidePadding;

    char title[64];
    snprintf(title, sizeof(title), "%s - Chapter", book->name);
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title, nullptr, true);

    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int cols = 5;
    const int colWidth = (pageWidth - 2 * metrics.contentSidePadding) / cols;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 8;
    const int maxY = renderer.getScreenHeight() - metrics.buttonHintsHeight - 10;

    for (int ch = 1; ch <= book->chapters && y < maxY; ++ch) {
        const int col = (ch - 1) % cols;
        const int cx = x + col * colWidth;

        if (col == 0 && ch > 1) {
            y += lineHeight;
            if (y >= maxY) break;
        }

        const bool selected = (ch == selectorIndex_);
        char num[8];
        snprintf(num, sizeof(num), "%d", ch);

        if (selected) {
            const int tw = renderer.getTextWidth(UI_10_FONT_ID, num);
            renderer.fillRect(cx - 2, y - 2, tw + 4, lineHeight - 4, true);
            renderer.drawText(UI_10_FONT_ID, cx, y, num, false);
        } else {
            renderer.drawText(UI_10_FONT_ID, cx, y, num, true);
        }
    }

    const auto labels = mappedInput.mapLabels("Back", "Select", "Prev", "Next");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void BibleActivity::renderJumpToRef() {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();

    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Jump to Reference", nullptr,
                   true);

    const int centerY = renderer.getScreenHeight() / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, centerY - 30, "Enter reference:");
    renderer.drawCenteredText(UI_12_FONT_ID, centerY + 10, refInput_.empty() ? "e.g. John 3:16" : refInput_.c_str());
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 60, "(Touch keyboard coming soon)");

    const auto labels = mappedInput.mapLabels("Cancel", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

}  // namespace margin
