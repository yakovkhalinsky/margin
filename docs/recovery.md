# Recovering a Bootlooping Xteink X4 Pro

If your X4 Pro is stuck in a bootloop after flashing margin firmware, this guide will help you restore it to a working state.

## Symptoms

- Device repeatedly shows the boot logo then resets
- No response to button presses
- Serial monitor shows: `invalid header: 0xffffffff` (indicates blank bootloader area)

## Quick Recovery (Web Flasher)

The easiest way to recover is using the web flasher:

1. Open https://yakov.khalinsky.com/margin/ in Chrome or Edge
2. Connect your X4 Pro via the pogo-pin USB dongle
3. Click **"Restore stock CrossPoint"**
4. Hold the Power button during the entire flash process
5. Wait for the flash to complete

## Manual Recovery with esptool

If the web flasher doesn't work, use esptool directly.

### Prerequisites

Install esptool:
```bash
pip install esptool
```

### Download Recovery Files

Download these files from the [latest release](https://github.com/yakovkhalinsky/margin/releases):
- `bootloader.bin` - ESP32-S3 bootloader
- `partitions.bin` - Partition table
- `boot_app0.bin` - OTA data initialization
- `crosspoint-x4pro-app.bin` - CrossPoint Reader application

Or download official CrossPoint firmware:
```bash
curl -L -o firmware-x4pro.bin \
  https://github.com/crosspoint-reader/crosspoint-reader/releases/latest/download/firmware-x4pro.bin
```

### Recovery Commands

#### Option 1: Flash Separate Parts (Recommended)

This method writes each component to its correct offset:

**Linux/macOS:**
```bash
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 crosspoint-x4pro-app.bin
```

**Windows:**
```cmd
py -m esptool --chip esp32s3 --port COM3 --baud 921600 ^
  write_flash ^
  0x0 bootloader.bin ^
  0x8000 partitions.bin ^
  0xe000 boot_app0.bin ^
  0x10000 crosspoint-x4pro-app.bin
```

#### Option 2: Flash App Only (If Bootloader is OK)

If only the app is corrupted but the bootloader works (device shows boot logo before crashing), you can flash just the app:

**Linux/macOS:**
```bash
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 firmware-x4pro.bin
```

**Windows:**
```cmd
py -m esptool --chip esp32s3 --port COM3 --baud 921600 ^
  write_flash 0x10000 firmware-x4pro.bin
```

#### Option 3: Restore from Full Backup

If you have a backup from before flashing:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0 backup.bin
```

### Troubleshooting

#### Device Not Detected

1. Make sure the pogo-pin USB dongle is properly connected
2. The X4 Pro should appear as `/dev/ttyACM0` (Linux), `/dev/cu.usbmodem*` (macOS), or `COM3` (Windows)
3. Check with `esptool --chip esp32s3 chip_id`

#### Flashing Fails or Times Out

1. **Hold the Power button** during the entire flash - the X4 Pro's bootloader doesn't latch power
2. Try entering bootloader mode manually:
   - Power off completely
   - Hold BOOT button (if present) while connecting USB
   - Or try holding Volume Down + Power
3. Reduce baud rate: `--baud 115200`

#### "Failed to connect" Error

The ESP32-S3's native USB can be finicky:
1. Disconnect and reconnect the USB cable
2. Try a different USB port (avoid hubs)
3. On Linux, ensure you have permissions: `sudo chmod 666 /dev/ttyACM0`

#### Verify Flash Contents

After flashing, verify the bootloader was written correctly:

```bash
esptool --chip esp32s3 --port /dev/ttyACM0 read_flash 0x0 32 /dev/stdout | xxd
```

You should see `e9` as the first byte (ESP32-S3 bootloader magic), not `ff`.

## Flash Layout Reference

| Component | Offset | Size | Description |
|-----------|--------|------|-------------|
| Bootloader | `0x0` | ~24 KB | ESP32-S3 second-stage bootloader |
| Partition Table | `0x8000` | 3 KB | Flash partition definitions |
| OTA Data | `0xe000` | 8 KB | Boot configuration (which app slot to run) |
| App (ota_0) | `0x10000` | ~6.25 MB | Main application |
| App (ota_1) | `0x650000` | ~6.25 MB | OTA update slot |
| SPIFFS | `0xc90000` | ~3.4 MB | Internal file storage |
| Coredump | `0xff0000` | 64 KB | Crash dump storage |

## Root Cause of v0.1.0 Bootloop

The v0.1.0 bootloop was caused by a failure in ESP Web Tools when flashing a merged binary to ESP32-S3 via native USB (USB-JTAG-Serial).

**What happened:**
1. ESP Web Tools performed a full chip erase (successful)
2. When writing the merged binary starting at offset 0x0, the writes to early addresses (bootloader at 0x0, partition table at 0x8000) failed
3. The app area at 0x10000 may have written partially
4. Result: Bootloader area was left blank (all 0xFF), causing the ROM to fail with "invalid header"

**Why it failed:**
ESP32-S3's native USB (USB-JTAG-Serial) has known buffer limitations. The esptool-js library that ESP Web Tools uses has a fixed `FLASH_WRITE_SIZE` of 16KB, but the USB-Serial-JTAG peripheral's buffer is smaller (`USB_RAM_BLOCK` = 2KB). Large writes can fail or corrupt data, especially at the beginning of flash.

**Prevention:**
- Flash separate parts instead of merged binaries
- Avoid full-chip erase (`new_install_prompt_erase: false`)
- Consider using `serialType: "cdc"` in manifests for ESP32-S3 native USB detection

## See Also

- [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
- [esptool documentation](https://docs.espressif.com/projects/esptool/en/latest/)
- [CrossPoint Reader recovery guide](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/docs/fix-bricked-xteink.md)
