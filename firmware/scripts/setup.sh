#!/usr/bin/env bash
# Prepare the CrossPoint Reader submodule for a margin build:
#   1. Initialize and update the submodule
#   2. Copy margin source files into the CrossPoint tree
#   3. Apply margin's hook patches
#   4. Link platformio.margin.ini as platformio.local.ini
# Idempotent: re-running resets the submodule and re-applies changes.
set -euo pipefail

FW="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$FW/.." && pwd)"
UP="$FW/upstream/crosspoint-reader"

echo "=== margin firmware setup ==="
echo "Firmware dir: $FW"
echo "CrossPoint dir: $UP"
echo ""

# Initialize submodule
echo "1. Initializing CrossPoint Reader submodule..."
git -C "$ROOT" submodule update --init firmware/upstream/crosspoint-reader

# Reset submodule to clean state
echo "2. Resetting submodule to clean state..."
git -C "$UP" reset --hard --quiet
git -C "$UP" clean -fdq -e platformio.local.ini

# Copy BibleActivity into CrossPoint's src tree
echo "3. Copying margin source files..."
mkdir -p "$UP/src/margin"
cp "$FW/lib/MarginBibleActivity/src"/* "$UP/src/margin/"

# Copy margin libraries
echo "4. Linking margin libraries..."
rm -rf "$UP/lib/MarginBible" 2>/dev/null
mkdir -p "$UP/lib/MarginBible"
ln -snf "$FW/lib/MarginBible/include" "$UP/lib/MarginBible/include" 2>/dev/null || \
    cp -r "$FW/lib/MarginBible/include" "$UP/lib/MarginBible/"
ln -snf "$FW/lib/MarginBible/src" "$UP/lib/MarginBible/src" 2>/dev/null || \
    cp -r "$FW/lib/MarginBible/src" "$UP/lib/MarginBible/"
cp "$FW/lib/MarginBible/library.json" "$UP/lib/MarginBible/"

# Apply margin integration
echo "5. Integrating margin into CrossPoint..."
AM_CPP="$UP/src/activities/ActivityManager.cpp"
AM_H="$UP/src/activities/ActivityManager.h"
HOME_CPP="$UP/src/activities/home/HomeActivity.cpp"

# Add BibleActivity include after last include in ActivityManager.cpp
if ! grep -q "MARGIN_ENABLE_BIBLE" "$AM_CPP" 2>/dev/null; then
    sed -i '/#include "util\/FullScreenMessageActivity.h"/a\
\
#ifdef MARGIN_ENABLE_BIBLE\
#include "margin/BibleActivity.h"\
#endif' "$AM_CPP"
    echo "   Added BibleActivity include to ActivityManager.cpp"
fi

# Add goToBible() method after goToCrashReport() in ActivityManager.cpp
if ! grep -q "goToBible" "$AM_CPP" 2>/dev/null; then
    sed -i '/void ActivityManager::goToCrashReport().*$/a\
\
#ifdef MARGIN_ENABLE_BIBLE\
void ActivityManager::goToBible() {\
  replaceActivity(std::make_unique<margin::BibleActivity>(renderer, mappedInput));\
}\
#endif' "$AM_CPP"
    echo "   Added goToBible() to ActivityManager.cpp"
fi

# Add goToBible() declaration after goHome() in ActivityManager.h
if ! grep -q "goToBible" "$AM_H" 2>/dev/null; then
    sed -i '/void goHome(HomeMenuItem initialMenuItem = HomeMenuItem::NONE, bool cleanInitialRefresh = false);/a\
\
#ifdef MARGIN_ENABLE_BIBLE\
  void goToBible();\
#endif' "$AM_H"
    echo "   Added goToBible() declaration to ActivityManager.h"
fi

# Add Bible to Home menu in HomeActivity.cpp
if ! grep -q "MARGIN_ENABLE_BIBLE" "$HOME_CPP" 2>/dev/null; then
    sed -i '/std::vector<UIIcon> menuIcons = {Folder, Library, Transfer, Settings};/a\
\
#ifdef MARGIN_ENABLE_BIBLE\
  menuItems.insert(menuItems.begin() + 2, "Bible");\
  menuIcons.insert(menuIcons.begin() + 2, Book);\
#endif' "$HOME_CPP"
    echo "   Added Bible to HomeActivity menu"
fi

# Link the margin config
echo "6. Linking platformio.margin.ini..."
ln -sf "$FW/platformio.margin.ini" "$UP/platformio.local.ini"

# Add generated files to exclude (handle submodule .git gitfile)
GIT_DIR="$UP/.git"
if [ -f "$GIT_DIR" ]; then
    # Resolve relative gitdir: reference from within $UP
    GIT_DIR="$UP/$(grep -oP 'gitdir: \K.*' "$GIT_DIR")"
    GIT_DIR="$(cd "$(dirname "$GIT_DIR")" && pwd)/$(basename "$GIT_DIR")"
fi
if [ -d "$GIT_DIR" ]; then
    EXCLUDE_FILE="$GIT_DIR/info/exclude"
    mkdir -p "$(dirname "$EXCLUDE_FILE")"
    for excl in platformio.local.ini src/margin lib/MarginBible; do
        grep -qxF "$excl" "$EXCLUDE_FILE" 2>/dev/null || echo "$excl" >> "$EXCLUDE_FILE"
    done
fi

echo ""
echo "=== Setup complete! ==="
echo ""
echo "To build:  cd $UP && pio run -e margin_x4pro"
echo "To flash:  esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 .pio/build/margin_x4pro/firmware.bin"
echo ""
echo "To revert to stock CrossPoint: use the web flasher at https://crosspointreader.com/#flash-tools"
