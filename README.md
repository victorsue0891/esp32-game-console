# ESP32-S3 Retro Handheld Game Console

基於 **ESP32-S3-DevKitC-1-N16R8** 的掌上型復古遊戲機，支援 **NES** 與 **GameBoy** 遊戲模擬。

## 特色

- **雙核心架構** — Core 0 跑 UI，Core 1 跑模擬器，兩者互不干擾
- **NES 模擬器** — 完整 6502 CPU + PPU + APU，支援 Mapper 0–4 (NROM/MMC1/UxROM/CNROM/MMC3)
- **GameBoy 模擬器** — 完整 LR35902 CPU + PPU + APU，支援 MBC1/MBC3/MBC5
- **LVGL 圖形選單** — 60/40 分割佈局，ROM 清單 + 預覽圖
- **三套主題** — Dark / Light / Retro (Game Boy 復古綠)，NVS 永存
- **WiFi OTA 更新** — 從 SD 卡讀取設定檔，背景下載不阻塞 UI
- **遊戲存檔** — 一鍵存檔/讀檔至 SD 卡，checksum 驗證 + 原子寫入
- **暫停選單** — Resume / Save / Load / Exit，Select+Start 觸發
- **電池監控** — ADC2 電壓讀取，設定畫面顯示百分比
- **FPS Overlay** — 模擬器畫面右上角即時顯示，可開關

## 硬體需求

| 元件 | 規格 |
|------|------|
| MCU | ESP32-S3-DevKitC-1-N16R8 (16MB Flash / 8MB PSRAM) |
| 螢幕 | 2.2" ILI9341 TFT LCD (240×320, SPI) |
| 音訊 | MAX98357 I2S DAC + 8Ω 喇叭 |
| 儲存 | Micro SD 卡 (FAT32, SPI 模式) |
| 輸入 | 10 個 GPIO 按鈕 |
| 電池 | 3.7V 鋰電池 + 分壓器 → GPIO15 |

## GPIO 對應

```
LCD (SPI2):  MOSI=11  MISO=13  SCK=12  CS=10  DC=9  RST=14
SD Card (SPI3): MOSI=35  MISO=37  SCK=36  CS=38
I2S Audio:   BCK=17  LRCK=18  DOUT=16
方向鍵:      UP=1  DOWN=2  LEFT=3  RIGHT=4
功能鍵:      A=5  B=6  START=7  SELECT=8
音量鍵:      VOL+=21  VOL-=47
電池 ADC:    GPIO15 (ADC2_CH4)
```

## 快速開始

### 環境安裝

1. 安裝 [VS Code](https://code.visualstudio.com/) + [PlatformIO 擴充套件](https://platformio.org/)
2. Clone 本專案

### 編譯與燒錄

```bash
# 編譯
pio run

# 燒錄
pio run --target upload

# 串列監控
pio device monitor
```

### SD 卡準備

在 SD 卡 (FAT32) 建立以下目錄結構：

```
/sdcard/
├── system/
│   ├── boot.jpg        ← 開機 Splash 畫面 (240×320)
│   └── wifi.cfg        ← WiFi 設定 (選用)
├── roms/
│   ├── nes/            ← 放入 .nes ROM 檔案
│   │   └── game.jpg    ← 同名預覽圖 (選用)
│   └── gb/             ← 放入 .gb ROM 檔案
│       └── game.jpg    ← 同名預覽圖 (選用)
└── saves/              ← 遊戲存檔 (自動建立)
```

### WiFi OTA 設定 (選用)

建立 `/sdcard/system/wifi.cfg`：

```
SSID=YourWiFiName
PASS=YourPassword
OTA_URL=http://your-server/firmware.bin
```

## 操作說明

| 操作 | 功能 |
|------|------|
| `UP/DOWN` | 選單導覽 |
| `A` | 確認 / 啟動遊戲 |
| `B` | 返回 |
| `START` | 開始 / 恢復遊戲 |
| `SELECT + START` | 暫停遊戲 → 開啟暫停選單 |
| `VOL+` / `VOL-` | 調整音量 (5% 步進) |

### 暫停選單

遊戲中按 `SELECT + START` 開啟：

- **Resume** — 繼續遊戲
- **Save State** — 存檔到 SD 卡
- **Load State** — 讀取之前的存檔
- **Exit Game** — 返回遊戲選單

### 設定畫面

從遊戲選單底部的 **⚙ Settings** 進入：

- **Theme** — 切換 Dark / Light / Retro 主題
- **FPS Overlay** — 開關遊戲中 FPS 顯示
- **WiFi OTA** — 觸發韌體更新
- **Battery** — 顯示電池百分比與電壓
- **Free Heap** — 顯示剩餘記憶體
- **Firmware** — 顯示目前韌體版本

## 專案架構

```
ESP32-Game-Console/
├── src/                     # 應用程式原始碼
│   ├── main.c               # 入口點、任務初始化
│   ├── emulator.c           # 模擬器整合 (ROM 載入/縮放/存檔)
│   ├── game_launcher.c      # 遊戲選單 UI
│   ├── pause_menu.c         # 暫停選單
│   ├── settings_screen.c    # 設定畫面
│   ├── lcd_driver.c         # ILI9341 SPI 驅動
│   ├── audio.c              # I2S 音訊驅動
│   ├── button.c             # GPIO 按鈕 (ISR + 去抖)
│   ├── sd_card.c            # SD 卡掛載
│   ├── splash.c             # 開機畫面
│   ├── volume_osd.c         # 音量 OSD
│   ├── ui_manager.c         # LVGL 初始化
│   ├── battery.c            # 電池 ADC 讀取
│   ├── wifi_manager.c       # WiFi STA 連線
│   ├── ota_update.c         # HTTP OTA 下載
│   ├── theme_manager.c      # 主題管理 (NVS)
│   └── perf_monitor.c       # FPS 追蹤 + Overlay
├── include/                 # 公開標頭檔
├── components/
│   ├── nes_emu/             # NES 模擬器核心
│   │   └── src/             # cpu, ppu, apu, cart, emu
│   └── gb_emu/              # GameBoy 模擬器核心
│       └── src/             # cpu, ppu, apu, cart, emu
├── platformio.ini           # PlatformIO 設定
├── partitions.csv           # Flash 分區表 (OTA 雙分區)
├── sdkconfig.defaults       # ESP-IDF SDK 預設設定
└── SPEC.md                  # 完整技術規格書
```

## 記憶體使用

| 資源 | 使用量 |
|------|--------|
| RAM | 71.8% (235 KB / 328 KB) |
| Flash | 16.1% (1.26 MB / 7.5 MB per OTA slot) |

## 技術細節

### 模擬器

- NES/GB 自動偵測 (iNES magic header / Nintendo logo checksum)
- ROM 載入至 PSRAM (最大 2MB)
- LUT 加速最近鄰縮放
- 16 行帶狀 SPI DMA 更新
- 固定點音量乘法 (>>8 bitshift)
- 非阻塞 I2S 寫入 (10ms timeout)

### 存檔系統

- 路徑: `/sdcard/saves/<rom_name>.sav`
- 格式: 4-byte magic + type + size + CRC checksum + data
- 原子寫入: 先寫 `.tmp` 再 rename，防止斷電損壞

### OTA 更新

- 雙 OTA 分區佈局 (ota_0 / ota_1, 各 7.5MB)
- HTTP streaming 下載 (4KB buffer)
- 支援 Rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
- 背景 FreeRTOS 任務，UI 不阻塞

## License

本專案以 **MIT License** 授權釋出，詳見 [LICENSE](LICENSE)。

遊戲 ROM 檔案受各自版權保護，需由使用者自行合法取得，本專案不提供任何 ROM 檔案。
