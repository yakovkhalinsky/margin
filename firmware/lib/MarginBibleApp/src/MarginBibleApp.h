// SPDX-License-Identifier: MIT
// The margin Bible reader, registered as a PapyriX MiniApp.
// Provides structured Bible reading with book/chapter/verse navigation.
#pragma once

#include "core/Types.h"

namespace papyrix {
struct Core;

namespace margin_bible_app {

void enter(Core& core);
bool update(Core& core);  // true when the screen needs a redraw
void onButton(Core& core, Button btn);
bool render(Core& core);  // true: the app already pushed the frame to the panel itself
void exit(Core& core);

}  // namespace margin_bible_app
}  // namespace papyrix
