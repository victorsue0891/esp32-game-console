# SPEC.md: ESP32-S3 Retro Handheld Game Console

## 1. Project Overview
本專案旨在開發一個基於 **ESP32-S3** 的掌上型遊戲機，能夠模擬 **NES (Famicom)** 與 **GameBoy (GB)** 遊戲。系統採用 **ESP-IDF** 框架開發，並使用 **LVGL** 構建現代化的圖形化選單系統。

### 1.1 Implementation Status
> **所有核心功能與三輪優化 (P0/P1/P2/P3) 均已完成實作並通過編譯驗證。**

| 類別 | 狀態 |
|------|------|
| 硬體驅動 (LCD/Audio/SD/Button) | ✅ 完成 |
| NES 模擬器 (CPU/PPU/APU/Cart) | ✅ 完成 (Mapper 0–4) |
| GameBoy 模擬器 (CPU/PPU/APU/Cart) | ✅ 完成 (MBC1/3/5) |
| LVGL UI (Splash/Launcher/OSD) | ✅ 完成 |
| WiFi OTA 韌體更新 | ✅ 完成 (背景任務) |
| 電池電量監控 | ✅ 完成 |
| 主題系統 (Dark/Light/Retro) | ✅ 完成 |
| 效能監控 (FPS Overlay) | ✅ 完成 |
| 遊戲存檔/讀檔 | ✅ 完成 |
| 遊戲暫停選單 | ✅ 完成 |
| 設定畫面 | ✅ 完成 |

## 2. Hardware Specifications
* **MCU:** ESP32-S3-DevKitC-1-N16R8 (16MB Flash / 8MB PSRAM)
* **Display:** 2.2" ILI9341 TFT LCD (240×320 resolution, SPI interface, 40MHz)
* **Audio:** MAX98357 I2S DAC connected to an 8Ω speaker (22050Hz, 16-bit mono)
* **Storage:** Micro SD Card (SPI mode, FAT32)
* **Input:** 10 GPIO buttons (UP, DOWN, LEFT, RIGHT, A, B, Start, Select, Vol+, Vol-)
* **Battery:** Lithium battery via ADC2 (GPIO15) with voltage divider (3.3V–4.2V)

## 3. Development Environment
* **IDE:** VS Code
* **Extension:** PlatformIO (Espressif 32 platform)
* **Framework:** ESP-IDF v5.2.1
* **Board:** esp32-s3-devkitc-1-n16r8v
* **UI Library:** LVGL v8.3.x

## 4. Software Architecture & Task Management
系統採用 FreeRTOS 多核心多任務架構：

* **Core 0 (System/UI Task):**
  - LVGL 渲染 (10ms 週期)
  - 按鈕 ISR + 去抖動任務
  - 音量 OSD 控制
  - 暫停選單 / 設定畫面

* **Core 1 (Emulator Task):**
  - NES/GB 模擬運算 (60fps 目標)
  - 幀緩衝縮放 (LUT 加速最近鄰)
  - I2S 音訊輸出 (非阻塞)
  - FPS 效能記錄

* **背景任務:**
  - OTA 韌體下載 (獨立 FreeRTOS task)

## 5. Functional Requirements

### 5.1 Boot Sequence
1. **NVS 初始化:** 載入主題偏好與系統設定。
2. **硬體驅動:** 按鈕 → LCD → SD 卡 → 音訊 → 電池 ADC。
3. **UI 系統:** LVGL 初始化、主題載入、效能監控啟動。
4. **Splash Screen:** 開機從 SD 卡讀取 `/system/boot.jpg` 並全螢幕顯示。
5. **Trigger:** 偵測到 `Start` 按鍵按下後，淡出進入遊戲選單。

### 5.2 Game Launcher (LVGL UI)
* **Layout:**
    * **左側 (60% 寬度):** `lv_list` 列表，掃描 SD 卡 `/roms/nes/` 與 `/roms/gb/` 內 ROM 檔案。
    * **右側 (40% 寬度):** `lv_img` 預覽圖，顯示與選中 ROM 同名的 `.jpg` 檔。
    * **底部:** ⚙ Settings 按鈕進入設定畫面。
* **Interaction:** 使用方向鍵 `UP/DOWN` 導覽列表，按下 `A` 按鍵啟動遊戲。
* **Features:** ROM 清單快取、主題色彩套用、最多 128 個 ROM。

### 5.3 Volume Control (OSD)
* **Default:** 30% Volume.
* **UI:** 螢幕左側顯示一個垂直的半透明 `lv_bar`。
* **Logic:**
    * 按下 `Vol+/Vol-` 時顯示 Bar 並更新數值（步進 5%）。
    * 若超過 5 秒未偵測到音量按鍵操作，Bar 自動消失（隱藏）。
* **Implementation:** 固定點音量乘法 (>>8 取代 /100)。

### 5.4 Emulation Core

#### NES Emulator (`components/nes_emu/`)
* **CPU:** 完整 6502 (含 unofficial opcodes) — `nes_cpu.c`
* **PPU:** 掃描線精確渲染 + 精靈評估優化 — `nes_ppu.c`
* **APU:** 4 聲道 (Pulse×2, Triangle, Noise) — `nes_apu.c`
* **Cart:** Mapper 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM), 4 (MMC3 + IRQ) — `nes_cart.c`
* **Output:** 256×240 RGB565 framebuffer, 22050Hz PCM audio

#### GameBoy Emulator (`components/gb_emu/`)
* **CPU:** 完整 LR35902 (含 CB-prefix opcodes) — `gb_cpu.c`
* **PPU:** 掃描線渲染 (BG/Window/Sprite, VRAM/OAM) — `gb_ppu.c`
* **APU:** 4 聲道 (Square×2, Wave, Noise) + Frame Sequencer — `gb_apu.c`
* **Cart:** MBC 自動偵測, MBC1/MBC3/MBC5 支援, Battery Save — `gb_cart.c`
* **Output:** 160×144 RGB565 framebuffer, 22050Hz PCM audio

#### Emulator Integration (`src/emulator.c`)
* ROM 格式自動偵測 (iNES header / GB Nintendo logo + checksum)
* PSRAM 分配 (ROM 最大 2MB + LCD framebuffer)
* LUT 加速幀縮放 (NES 256×240→LCD 240×225, GB 160×144→LCD 240×216)
* 16 行帶狀 DMA 更新
* 60fps 幀率控制 + FPS overlay
* Semaphore 同步安全停止

### 5.5 Pause Menu
* **Trigger:** 遊戲中按 `Select + Start` 暫停並顯示選單。
* **Options:**
  - ▶ **Resume** — 恢復遊戲
  - 💾 **Save State** — 存檔至 SD 卡 (`/saves/<rom>.sav`)
  - 📤 **Load State** — 讀取存檔
  - ✕ **Exit Game** — 返回遊戲選單
* **Save Format:** Header (magic + type + size + checksum) + framebuffer data，原子寫入防損壞。

### 5.6 Settings Screen
* **Theme:** Dark / Light / Retro (NVS 永存)
* **FPS Overlay:** 開關遊戲中 FPS 顯示
* **WiFi OTA:** 讀取 `/system/wifi.cfg` 連線並下載韌體 (背景任務)
* **System Info:** 電池電量/電壓、可用 Heap、韌體版本
* **Auto-refresh:** 每 2 秒更新動態數值

### 5.7 WiFi OTA Update
* **Config file:** `/sdcard/system/wifi.cfg` (格式: `SSID=`, `PASS=`, `OTA_URL=`)
* **Process:** WiFi STA 連線 → HTTP 下載 → OTA 分區寫入 → 自動重啟
* **Partition:** 雙 OTA 分區 (ota_0 + ota_1, 各 7.5MB)，支援 Rollback
* **UI:** 非阻塞背景任務，即時進度條回饋

### 5.8 Battery Monitoring
* **ADC:** GPIO15 (ADC2_CH4) + 2:1 電壓分壓器
* **Calibration:** ESP32-S3 curve fitting / line fitting
* **Range:** 3.3V (0%) – 4.2V (100%)
* **WiFi Conflict:** ADC2 暫停/恢復機制（WiFi 啟動時回傳快取值）

### 5.9 Performance Monitor
* **FPS Tracking:** 原子計數器，每秒統計
* **Overlay:** 3×5 像素迷你字型，模擬器 framebuffer 右上角半透明顯示
* **可從設定畫面開關**

## 6. GPIO Mapping
| Component | Function | ESP32-S3 GPIO |
| :--- | :--- | :--- |
| **LCD** | MOSI/MISO/SCK/CS/DC/RST | 11/13/12/10/9/14 |
| **SD Card** | MOSI/MISO/SCK/CS | 35/37/36/38 |
| **I2S Audio** | BCK/LRCK/DOUT | 17/18/16 |
| **Buttons** | UP/DOWN/LEFT/RIGHT | 1/2/3/4 |
| **Buttons** | A/B/Start/Select | 5/6/7/8 |
| **Buttons** | Vol+/Vol- | 21/47 |
| **Battery** | ADC2_CH4 | 15 |

## 7. Flash Partition Layout (16MB, OTA-enabled)
| Partition | Type | Offset | Size |
| :--- | :--- | :--- | :--- |
| nvs | NVS | 0x9000 | 24 KB |
| otadata | OTA Data | 0xF000 | 8 KB |
| phy_init | PHY | 0x11000 | 4 KB |
| ota_0 | App (OTA) | 0x20000 | 7.5 MB |
| ota_1 | App (OTA) | 0x7A0000 | 7.5 MB |
| storage | SPIFFS | 0xF20000 | 896 KB |

## 8. SD Card Directory Structure
```
/sdcard/
├── system/
│   ├── boot.jpg          # 開機 Splash 畫面
│   └── wifi.cfg          # WiFi 設定 (SSID/PASS/OTA_URL)
├── roms/
│   ├── nes/              # NES ROM 檔案 (.nes)
│   │   └── *.jpg         # 同名預覽圖
│   └── gb/               # GameBoy ROM 檔案 (.gb)
│       └── *.jpg         # 同名預覽圖
└── saves/                # 遊戲存檔 (.sav)
```

## 9. Memory Usage (Last Build)
| Resource | Usage |
| :--- | :--- |
| **RAM** | 71.8% (235,292 / 327,680 bytes) |
| **Flash** | 16.1% (1,262,605 / 7,864,320 bytes per OTA slot) |

## 10. Build & Flash
```bash
# Build
pio run

# Upload
pio run --target upload

# Monitor serial output
pio device monitor
```
