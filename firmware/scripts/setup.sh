#!/usr/bin/env bash
# Prepare the CrossPoint Reader submodule for a margin build:
#   1. Initialize and update the submodule
#   2. Copy margin source files into the CrossPoint tree
#   3. Apply margin branding overlay (od3sa cyber bear logo)
#   4. Apply margin's hook patches
#   5. Link platformio.margin.ini as platformio.local.ini
# Idempotent: re-running resets the submodule and re-applies changes.
set -euo pipefail

FW="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$FW/.." && pwd)"
UP="$FW/upstream/crosspoint-reader"
BRANDING="$FW/branding"

echo "=== margin firmware setup ==="
echo "Firmware dir: $FW"
echo "CrossPoint dir: $UP"
echo "Branding dir: $BRANDING"
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

# Apply margin branding overlay (od3sa cyber bear logo)
echo "5. Applying margin branding..."
BRANDING_GEN="$BRANDING/generated"
if [ -d "$BRANDING_GEN" ]; then
    # Generate branding if not already done
    if [ ! -f "$BRANDING_GEN/Logo120.h" ] && [ -f "$BRANDING/generate_branding.py" ]; then
        echo "   Generating branding assets..."
        python3 "$BRANDING/generate_branding.py" >/dev/null 2>&1 || {
            echo "   Warning: Could not generate branding assets (missing dependencies?)"
            echo "   Install with: pip install Pillow cairosvg"
        }
    fi
    
    # Replace Logo120.h for boot splash and sleep screen
    if [ -f "$BRANDING_GEN/Logo120.h" ]; then
        cp "$BRANDING_GEN/Logo120.h" "$UP/src/images/Logo120.h"
        echo "   Replaced Logo120.h (boot/sleep screen logo)"
    fi
    
    # Replace web UI favicon/icon
    if [ -f "$BRANDING_GEN/margin-icon.svg" ]; then
        cp "$BRANDING_GEN/margin-icon.svg" "$UP/src/network/html/assets/crosspoint-icon.svg"
        echo "   Replaced crosspoint-icon.svg (web UI favicon)"
    fi
else
    echo "   Warning: Branding directory not found, skipping branding overlay"
fi

# Apply margin integration
echo "6. Integrating margin into CrossPoint..."
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

# Copy companion Activities into CrossPoint's src tree
echo "   Copying companion Activities..."
if [ -d "$FW/lib/MarginNotificationsActivity" ]; then
    cp "$FW/lib/MarginNotificationsActivity/src"/* "$UP/src/margin/"
fi
if [ -d "$FW/lib/MarginMessagesActivity" ]; then
    cp "$FW/lib/MarginMessagesActivity/src"/* "$UP/src/margin/"
fi
if [ -d "$FW/lib/MarginCalendarActivity" ]; then
    cp "$FW/lib/MarginCalendarActivity/src"/* "$UP/src/margin/"
fi

# Add companion Activity includes to ActivityManager.cpp
if ! grep -q "MARGIN_BLE" "$AM_CPP" 2>/dev/null; then
    sed -i '0,/#ifdef MARGIN_ENABLE_BIBLE/{s/#ifdef MARGIN_ENABLE_BIBLE/#ifdef MARGIN_BLE\
#include "margin\/NotificationsActivity.h"\
#include "margin\/MessagesActivity.h"\
#include "margin\/CalendarActivity.h"\
#endif\
\
#ifdef MARGIN_ENABLE_BIBLE/}' "$AM_CPP"
    echo "   Added companion Activity includes to ActivityManager.cpp"
fi

# Add companion goToXxx() methods to ActivityManager.cpp
if ! grep -q "goToCompanionNotifications" "$AM_CPP" 2>/dev/null; then
    sed -i '0,/#ifdef MARGIN_ENABLE_BIBLE/{s/#ifdef MARGIN_ENABLE_BIBLE/#ifdef MARGIN_BLE\
void ActivityManager::goToCompanionNotifications() {\
  replaceActivity(std::make_unique<margin::NotificationsActivity>(renderer, mappedInput));\
}\
void ActivityManager::goToCompanionMessages() {\
  replaceActivity(std::make_unique<margin::MessagesActivity>(renderer, mappedInput));\
}\
void ActivityManager::goToCompanionCalendar() {\
  replaceActivity(std::make_unique<margin::CalendarActivity>(renderer, mappedInput));\
}\
#endif\
\
#ifdef MARGIN_ENABLE_BIBLE/}' "$AM_CPP"
    echo "   Added companion goToXxx() methods to ActivityManager.cpp"
fi

# Add companion method declarations to ActivityManager.h
if ! grep -q "goToCompanionNotifications" "$AM_H" 2>/dev/null; then
    sed -i '0,/#ifdef MARGIN_ENABLE_BIBLE/{s/#ifdef MARGIN_ENABLE_BIBLE/#ifdef MARGIN_BLE\
  void goToCompanionNotifications();\
  void goToCompanionMessages();\
  void goToCompanionCalendar();\
#endif\
\
#ifdef MARGIN_ENABLE_BIBLE/}' "$AM_H"
    echo "   Added companion method declarations to ActivityManager.h"
fi

# Add companion items to Home menu in HomeActivity.cpp
# Using available icons: Blocks for calendar, Text for messages, Recent for notifications
if ! grep -q "MARGIN_BLE" "$HOME_CPP" 2>/dev/null; then
    sed -i '0,/#ifdef MARGIN_ENABLE_BIBLE/{s/#ifdef MARGIN_ENABLE_BIBLE/#ifdef MARGIN_BLE\
  menuItems.insert(menuItems.begin() + 2, "Calendar");\
  menuIcons.insert(menuIcons.begin() + 2, Blocks);\
  menuItems.insert(menuItems.begin() + 2, "Messages");\
  menuIcons.insert(menuIcons.begin() + 2, Text);\
  menuItems.insert(menuItems.begin() + 2, "Notifications");\
  menuIcons.insert(menuIcons.begin() + 2, Recent);\
#endif\
\
#ifdef MARGIN_ENABLE_BIBLE/}' "$HOME_CPP"
    echo "   Added companion items to HomeActivity menu"
fi

# Link companion libraries
echo "   Linking companion libraries..."
for lib in MarginCompanion MarginBle MarginProtocol; do
    if [ -d "$FW/lib/$lib" ]; then
        rm -rf "$UP/lib/$lib" 2>/dev/null
        mkdir -p "$UP/lib/$lib"
        if [ -d "$FW/lib/$lib/include" ]; then
            ln -snf "$FW/lib/$lib/include" "$UP/lib/$lib/include" 2>/dev/null || \
                cp -r "$FW/lib/$lib/include" "$UP/lib/$lib/"
        fi
        if [ -d "$FW/lib/$lib/src" ]; then
            ln -snf "$FW/lib/$lib/src" "$UP/lib/$lib/src" 2>/dev/null || \
                cp -r "$FW/lib/$lib/src" "$UP/lib/$lib/"
        fi
        if [ -f "$FW/lib/$lib/library.json" ]; then
            cp "$FW/lib/$lib/library.json" "$UP/lib/$lib/"
        fi
    fi
done

# Link the margin config
echo "7. Linking platformio.margin.ini..."
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
