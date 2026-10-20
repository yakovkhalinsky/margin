// SPDX-License-Identifier: MIT
// Bible reader Activity for margin on CrossPoint Reader.
// Provides structured Bible reading with book/chapter/verse navigation.
#pragma once

#include "activities/Activity.h"
#include "margin/bible.h"

namespace margin {

class BibleActivity final : public Activity {
 public:
    explicit BibleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
    ~BibleActivity() override = default;

    void onEnter() override;
    void onExit() override;
    void loop() override;
    void render(RenderLock&&) override;

 private:
    enum class Mode {
        Reading,       // Normal verse reading
        BookSelect,    // Selecting a book
        ChapterSelect, // Selecting a chapter
        JumpToRef,     // Jump-to-reference input
    };

    struct ChapterCache {
        uint8_t bookIndex = 0;
        uint16_t chapter = 0;
        std::vector<std::string> lines;
        int topLine = 0;
        bool loaded = false;

        void clear() {
            lines.clear();
            topLine = 0;
            loaded = false;
        }
    };

    void renderReading();
    void renderBookSelect();
    void renderChapterSelect();
    void renderJumpToRef();

    void handleReadingInput();
    void handleBookSelectInput();
    void handleChapterSelectInput();
    void handleJumpToRefInput();

    bool loadChapter(uint8_t bookIndex, uint16_t chapter);
    void savePosition();
    void loadPosition();

    bible::NavState nav_;
    Mode mode_ = Mode::Reading;
    ChapterCache cache_;
    int selectorIndex_ = 0;

    // Jump-to-reference input state
    std::string refInput_;
    int refCursor_ = 0;
};

}  // namespace margin
