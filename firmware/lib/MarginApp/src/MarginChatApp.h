// SPDX-License-Identifier: MIT
// The margin Chat screen, registered as a PapyriX MiniApp (see firmware/patches/0001-*.patch).
// It has the same shape as upstream apps such as PomodoroApp so it plugs into AppLauncherState.
#pragma once

#include "core/Types.h"

namespace papyrix {
struct Core;

namespace margin_chat_app {
void enter(Core& core);
bool update(Core& core);  // true when the screen needs a redraw
void onButton(Core& core, Button btn);
bool render(Core& core);  // true: the app already pushed the frame to the panel itself
void exit(Core& core);
}  // namespace margin_chat_app
}  // namespace papyrix
