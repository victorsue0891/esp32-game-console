# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This project uses **PlatformIO** with **ESP-IDF v5.5.1** (IDF path configured at `D:\tools\esp\v5.5.1\esp-idf` in `.vscode/settings.json`).

```bash
pio run                         # Compile
pio run --target upload         # Flash to device
pio device monitor              # Serial console at 115200 baud
pio run --target menuconfig     # ESP-IDF SDK configuration menu
```

There are no unit tests in this project.

## Architecture

### Dual-Core Task Model

The fundamental design split is Core 0 vs Core 1:

- **Core 0** — UI task: LVGL rendering (10ms tick), button polling, OSD overlays, pause menu, settings screen
- **Core 1** — Emulator task: NES/GB emulation at 60fps, frame scaling, I2S audio output

### Module Hierarchy

```
main.c  (task creation, button event routing, boot sequence)
  ├── Hardware drivers: lcd_driver, sd_card, audio, button, battery
  ├── UI layer: ui_manager (LVGL init) → game_launcher, pause_menu, settings_screen
  │             theme_manager (NVS), volume_osd, perf_monitor, splash
  ├── emulator.c  (ROM type detection, PSRAM allocation, frame scaling LUTs, save states)
  │   ├── components/nes_emu/  (nes_cpu, nes_ppu, nes_apu, nes_cart)
  │   └── components/gb_emu/  (gb_cpu, gb_ppu, gb_apu, gb_cart)
  └── Network: wifi_manager, ota_update
```

### Key Architectural Details

- **Emulator vtable** (`include/emulator_driver.h`): `emulator_driver_t` struct holds all per-system function pointers (`validate`, `init`, `run_frame`, `set_input`, `get_framebuffer`, `get_audio`, `save_state`, `load_state`, `shutdown`). Adding a new system requires implementing the vtable and adding one entry to `drivers[]` in `emulator.c` — no other file changes.
- **ROM loading** (`emulator.c`): driver selected by file extension via `find_driver_by_ext()`, ROM loaded into PSRAM, validated via `driver->validate()`, then passed to `driver->init()`. Integer scaling LUTs built once per driver for 240×320→emulator-native resolution mapping.
- **Double-framebuffer display pipeline**: two PSRAM buffers (`lcd_fb[2]`), producer-consumer flow controlled by `buf_free_sem` (counting semaphore, initial count 2) and `disp_queue` (queue size 1). Emulator task (Core 1, prio 6) writes frames non-blocking; dedicated `lcd_flush_task` (Core 1, prio 5) flushes to LCD via SPI2 DMA at 40MHz. Emulator skips display for a frame if both slots are in use but continues emulation and audio at full speed (~26fps display, 60fps emulation).
- **Audio pipeline + A/V sync**: emulator APU fills sample buffer → `audio.c` writes to I2S DMA queue (10ms timeout) → MAX98357 DAC at 22050Hz. `av_credit` (atomic int, 0–4) tracks I2S queue health; emulator yields 2ms when credit > 2 to let the LCD task catch up.
- **Button events**: GPIO ISR with debounce → callback registered in `main.c` routes to active context (game_launcher / emulator / pause_menu).
- **Settings persistence**: NVS (non-volatile storage) used for theme, volume, and WiFi credentials via `theme_manager.c` and `settings_screen.c`.
- **OTA updates**: configured via `/system/wifi.cfg` on SD card (SSID/PASS/OTA_URL), writes to alternate OTA partition; `esp_ota_mark_app_valid_cancel_rollback()` called in `main.c` after hardware init to prevent rollback on next reboot.

### Hardware Pinout (from `include/config.h`)

| Peripheral | Pins |
|-----------|------|
| LCD ILI9341 (SPI2) | MOSI=11, MISO=13, SCK=12, CS=10, DC=9, RST=14 |
| SD Card (SPI3) | MOSI=35, MISO=37, SCK=36, CS=38 |
| I2S Audio | BCK=17, LRCK=18, DOUT=16 |
| Buttons | UP=1, DOWN=2, LEFT=3, RIGHT=4, A=5, B=6, START=7, SELECT=8, VOL+=21, VOL-=47 |
| Battery ADC | GPIO15 (ADC2_CH4, 2:1 voltage divider) |

### Flash Partitions (`partitions.csv`)

OTA-enabled layout: two 7.5MB app slots (OTA_0 @ 0x20000, OTA_1 @ 0x7A0000), NVS, SPIFFS storage partition.

### Emulator Components

Both emulators live under `components/` as ESP-IDF components with their own `CMakeLists.txt`:

- **`nes_emu`**: Mappers 0–4 (NROM/MMC1/UxROM/CNROM/MMC3), covers ~95% of NES library
- **`gb_emu`**: MBC1/MBC3/MBC5 with battery save support, covers ~98% of GB library

### SD Card Layout Expected by Firmware

```
/sdcard/
  roms/nes/       # NES ROMs (.nes)
  roms/gb/        # GameBoy ROMs (.gb)
  saves/          # Save states
  system/wifi.cfg  # WiFi credentials for OTA
  system/boot.jpg  # Optional boot splash image
```

## Memory Constraints

RAM usage is ~72% (235 KB / 328 KB). ROM data and framebuffers must go in PSRAM (8MB octal). Be careful adding stack-allocated buffers or large globals in RAM.
