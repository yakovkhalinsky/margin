# Hardware: Xteink X4 Pro

margin targets **only the Xteink X4 Pro**. It is *not* the ESP32-C3 based X4 / X3: different chip,
different board, different firmware image. Do not flash X4/X3 images on it or vice versa.

Legend: ✅ confirmed by two independent sources or by upstream code that runs on the device ·
⚠️ single source / vendor claim · ❓ unverified or conflicting.

## Summary

| Item | Value | Confidence | Source |
|---|---|---|---|
| SoC | ESP32-S3, dual-core Xtensa LX7 @ 240 MHz (rev v0.2 on Explorink's unit) | ✅ | Explorink, PapyriX |
| PSRAM | 8 MB (PapyriX builds it as **octal**, `qio_opi`; Explorink says "embedded") | ✅ size / ⚠️ type | PapyriX `platformio.ini`, Explorink |
| Flash | 16 MB | ✅ | Explorink, PapyriX |
| Display | 4.3" e-paper, 800 × 480 native (driven as 480 × 800 portrait), 219 PPI | ✅ / ⚠️ PPI | Explorink, PapyriX, Xteink |
| Panel controller | **Varies by unit**: UC8279, UC8179, or SSD1677. Firmware must probe and select a driver at boot | ✅ varies | Explorink, PapyriX |
| Touch | Capacitive, GT911 @ I²C 0x5D | ✅ | Explorink, PapyriX |
| Front light | Two PWM channels, warm (GPIO9) and cool (GPIO8) | ✅ | Explorink, PapyriX |
| Buttons | Page buttons on both sides (Up GPIO0, Down GPIO7), Power GPIO3, capacitive home key on the front | ✅ / ❓ home key wiring | Explorink, PapyriX |
| Battery | 1100 mAh, CW2017 fuel gauge, charge status on GPIO21 | ⚠️ capacity (vendor) | Xteink, PapyriX |
| RTC | BM8563 @ 0x51 | ⚠️ | PapyriX |
| Storage | microSD (1-bit SDMMC; 16 GB card included, up to 256 GB) | ✅ | Xteink, PapyriX |
| Radio | 2.4 GHz Wi-Fi + Bluetooth 5 LE | ✅ | Xteink, Explorink |
| USB | **No socket.** USB data and charging only through the pogo pins and the magnetic dongle in the box. Native USB-Serial/JTAG, VID:PID `303A:1001` | ✅ | Xteink, Explorink, PapyriX |
| Size / weight | 111 × 69 × 5.95 mm, 72 g | ⚠️ vendor | Xteink |

## Pin map (from PapyriX `docs/x4pro-specifications.md`)

| Function | Pins |
|---|---|
| Display SPI | SCLK 12, MOSI 11, CS 13, DC 18, RESET 14, BUSY 6 |
| microSD (SDMMC 1-bit) | CLK 41, CMD 42, DAT0 40; rail enable GPIO5 (active low) |
| Touch GT911 | SDA 39, SCL 38, IRQ 10, RESET 4; rail GPIO2 (active low); 100 kHz |
| Fuel gauge CW2017 | shares I²C 39/38; charge status GPIO21 |
| Front light | cool GPIO8, warm GPIO9 (PWM) |
| Buttons | Up 0, Down 7, Power 3 |
| Power latch | GPIO1, active high (the app must assert it at boot) |

margin does not touch these directly; it reuses PapyriX's `BoardSupport` / `EInkDisplay` / HAL code.

## Panel controller detection

Explorink reports three different answers for one product: Xteink's own board ID names an SSD1677,
their unit read back a UC8279, and earlier benched units carried a UC8179. PapyriX handles this at
boot: it selects UC8279 for LUT versions `0x02`, `0x68`, `0x69`, UC8179 for `0x01`, and falls back to
SSD1677 otherwise. margin inherits that logic unchanged.

## Refresh behaviour (matters for streaming chat)

- Measured on one unit (Explorink, Sept 2026): **~488 ms fast refresh, ~1,344 ms clean full refresh**.
- PapyriX's X4 Pro drivers (`Uc8179X4ProDriver`, `Uc8279X4ProDriver`) expose **Full / Half / Fast**
  modes. Only the SSD1677 driver has a **Window** (region) mode. ❓ Whether UC8179/UC8279 panels on
  the X4 Pro can do true windowed partial updates is unverified.
- So margin's first "partial refresh" is **coalesced Fast refreshes of the whole frame** (at most one
  per ~400 ms while text streams, a Full/Half every N Fast refreshes to clear ghosting). Windowed
  updates of just the bottom of the transcript are a research item in [ROADMAP.md](ROADMAP.md) (M3).

## Flashing and recovery

> **Read this before flashing anything.**

1. **Back up the full 16 MB flash first.** There is no official Xteink firmware download, so your
   backup is the only way back to stock.
   ```bash
   # hold Power, connect the pogo dongle, then:
   esptool --chip esp32s3 --port /dev/ttyACM0 read-flash 0 0x1000000 x4pro-stock-16MB.bin
   # (esptool < 5 spells it read_flash)
   ```
   Keep two copies somewhere safe. To restore: `esptool --chip esp32s3 write-flash 0 x4pro-stock-16MB.bin`.
2. **Buy from xteink.com.** Xteink states units bought directly are exempt from third-party firmware
   restrictions; some marketplace units have shipped USB-locked.
3. **Use the pogo-pin dongle.** Without it the X4 Pro does not enumerate at all.
4. **Hold Power for the whole flash.** The ROM download path does not assert the power latch (GPIO1);
   release after the new firmware has booted.
5. The application image goes to `0x10000` (`esptool --chip esp32s3 write-flash 0x10000 <image>.bin`),
   same as PapyriX's `papyrix-x4pro.bin`.

## Sources

- Explorink, "Xteink X4 Pro, the reader we now build against": https://explorink.com/hardware/xteink-x4-pro/
- Xteink, "X4 Pro FAQ: Formats, Storage, Charging and Support": https://www.xteink.com/blogs/product/x4-pro-faq-specs-support
- PapyriX, `docs/x4pro-specifications.md`, `docs/s3-platform.md`, `platformio.ini` at the pinned commit
  (`firmware/upstream/papyrix-reader`).

## Open questions

- ❓ How the capacitive home key is read (GT911 key channel or a separate pad).
- ❓ Windowed partial refresh on UC8179 / UC8279 X4 Pro panels.
- ❓ BLE + front light battery drain (early units were criticised for front-light drain).
- ❓ Whether NimBLE coexists cleanly with PapyriX's Wi-Fi features (LocalSend, Calibre, web server)
  when both radios are on; margin's default is to keep Wi-Fi off.
