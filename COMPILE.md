# 編譯與燒錄指南

本文件說明如何從零開始建立開發環境、編譯韌體，並燒錄至 ESP32-S3 掌機。

---

## 目錄

1. [系統需求](#1-系統需求)
2. [安裝開發工具](#2-安裝開發工具)
3. [取得原始碼](#3-取得原始碼)
4. [首次編譯](#4-首次編譯)
5. [燒錄韌體](#5-燒錄韌體)
6. [串列監控（除錯）](#6-串列監控除錯)
7. [常用指令速查](#7-常用指令速查)
8. [故障排除](#8-故障排除)
9. [CI / 自動化建置](#9-ci--自動化建置)

---

## 1. 系統需求

| 項目 | 最低需求 |
|------|---------|
| 作業系統 | Windows 10/11、macOS 12+、Ubuntu 20.04+ |
| Python | 3.9 以上 |
| 磁碟空間 | 約 5 GB（工具鏈 + SDK） |
| USB | Type-C 連接埠（用於燒錄） |

---

## 2. 安裝開發工具

### 2-1. 安裝 VS Code（建議）

前往 [https://code.visualstudio.com](https://code.visualstudio.com) 下載並安裝。

### 2-2. 安裝 PlatformIO 擴充套件

1. 開啟 VS Code
2. 按 `Ctrl+Shift+X` 開啟擴充套件面板
3. 搜尋 `PlatformIO IDE` → 安裝

> 安裝完成後 VS Code 會自動下載 PlatformIO Core（約需 2–5 分鐘）。

### 2-3. 命令列安裝（無 VS Code 亦可）

```bash
pip install platformio
```

驗證安裝：

```bash
pio --version
# PlatformIO Core, version 6.x.x
```

### 2-4. USB 驅動程式

ESP32-S3-DevKitC-1 使用板載 USB-JTAG，**不需要額外安裝 CH340 / CP2102 驅動**。

- **Windows**：若裝置管理員顯示未知裝置，安裝 [WinUSB](https://zadig.akeo.ie/) 並選擇 `USB JTAG/serial debug unit`。
- **Linux**：將使用者加入 `dialout` 群組：
  ```bash
  sudo usermod -a -G dialout $USER
  # 登出再登入後生效
  ```
- **macOS**：無需額外設定。

---

## 3. 取得原始碼

```bash
git clone https://github.com/victorsue0891/esp32-game-console.git
cd esp32-game-console
```

專案結構說明：

```
esp32-game-console/
├── src/                  # 應用程式原始碼
├── include/              # 公開標頭檔
├── components/
│   ├── nes_emu/          # NES 模擬器核心
│   └── gb_emu/           # Game Boy 模擬器核心
├── boards/               # 自訂板子定義（esp32-s3-devkitc-1-n16r8v）
├── platformio.ini        # PlatformIO 設定
└── partitions.csv        # Flash 分區表
```

---

## 4. 首次編譯

### 4-1. 使用 VS Code

1. 以 VS Code 開啟專案資料夾（`File > Open Folder`）
2. 等待 PlatformIO 初始化（底部狀態列顯示完成）
3. 點擊底部工具列的 **✓ Build** 按鈕

### 4-2. 使用命令列

```bash
pio run
```

**首次執行**會自動下載：
- `espressif32@6.7.0` 平台套件（~200 MB）
- ESP-IDF v5.2.1 工具鏈（~1.5 GB）
- LVGL 函式庫

完成後輸出範例：

```
RAM:   [=======   ]  71.6% (used 234604 bytes from 327680 bytes)
Flash: [==        ]  16.1% (used 1269825 bytes from 7864320 bytes)
======================== [SUCCESS] Took 150 seconds ========================
```

> 首次下載工具鏈約需 10–20 分鐘，後續編譯約 2–3 分鐘（僅重新編譯修改過的檔案）。

---

## 5. 燒錄韌體

### 5-1. 連接硬體

1. 用 USB Type-C 線連接 ESP32-S3-DevKitC-1 至電腦
2. 確認作業系統已識別裝置（Windows：裝置管理員出現 COM 埠；Linux/macOS：`/dev/ttyACM0` 或 `/dev/cu.usbmodem...`）

### 5-2. 燒錄

```bash
pio run --target upload
```

PlatformIO 會自動偵測 COM 埠並燒錄。若有多個裝置，可在 `platformio.ini` 指定：

```ini
upload_port = COM3          # Windows
# upload_port = /dev/ttyACM0  # Linux
```

### 5-3. 燒錄流程說明

本專案採用 **OTA 雙分區**設計，首次燒錄寫入 `ota_0`（offset `0x20000`）：

```
Flash 16MB
┌────────────┐ 0x009000  NVS (24 KB)
├────────────┤ 0x00F000  OTA Data (8 KB)
├────────────┤ 0x020000
│   ota_0   │           7.5 MB ← 首次燒錄於此
├────────────┤ 0x7A0000
│   ota_1   │           7.5 MB ← OTA 更新寫入此處
└────────────┘
```

### 5-4. 強制進入燒錄模式（若自動燒錄失敗）

1. 按住 `BOOT` 按鈕
2. 按一下 `RESET` 按鈕
3. 放開 `BOOT` 按鈕
4. 執行 `pio run --target upload`

---

## 6. 串列監控（除錯）

```bash
pio device monitor
```

預設鮑率：**115200**，輸出範例：

```
I (1234) emulator: [NES] emulator started
I (1235) nes_emu: NES emulator initialized
I (2000) audio: I2S audio initialized (sample rate: 22050 Hz)
```

同時編譯並燒錄後開啟監控：

```bash
pio run --target upload && pio device monitor
```

按 `Ctrl+C` 離開監控。

---

## 7. 常用指令速查

| 目的 | 指令 |
|------|------|
| 編譯 | `pio run` |
| 燒錄 | `pio run --target upload` |
| 串列監控 | `pio device monitor` |
| 編譯 + 燒錄 + 監控 | `pio run --target upload && pio device monitor` |
| 清除編譯快取 | `pio run --target clean` |
| 完整重新編譯 | `pio run --target clean && pio run` |
| SDK 設定選單 | `pio run --target menuconfig` |
| 列出可用裝置 | `pio device list` |

---

## 8. 故障排除

### 找不到裝置 / 無法燒錄

```
Error: COM port not found
```

- 確認 USB 線有資料傳輸功能（部分充電線無資料線）
- Windows：檢查裝置管理員是否有黃色驚嘆號，若有請安裝驅動
- Linux：執行 `ls /dev/ttyACM*` 確認裝置存在，並確認已加入 `dialout` 群組

### 燒錄失敗 / 超時

```
A fatal error occurred: Failed to connect to ESP32-S3
```

手動進入燒錄模式（見 [5-4](#5-4-強制進入燒錄模式若自動燒錄失敗)）。

### 編譯錯誤：找不到板子

```
UnknownBoard: Unknown board ID 'esp32-s3-devkitc-1-n16r8v'
```

確認專案根目錄有 `boards/esp32-s3-devkitc-1-n16r8v.json`。若不存在，請重新 clone 或：

```bash
git checkout boards/
```

### 工具鏈下載失敗

網路不穩時下載可能中斷，執行以下指令清除快取後重試：

```bash
pio run --target clean
rm -rf ~/.platformio/packages/framework-espidf
pio run
```

### 記憶體不足（編譯期間）

本機 RAM < 4 GB 可能在 ESP-IDF CMake 階段 OOM。建議：
- 關閉其他佔用記憶體的程式
- 或在 `platformio.ini` 加入 `-j2` 限制並行編譯數：
  ```ini
  build_flags = ... -j2
  ```

---

## 9. CI / 自動化建置

本專案已設定 GitHub Actions，每次 push 或 PR 自動執行完整編譯：

```
.github/workflows/build.yml
```

- 成功後自動上傳 `firmware.bin` 作為 Artifact（保留 30 天）
- 可從 GitHub Actions 頁面直接下載，無需本機環境即可取得最新韌體
- 也可手動觸發：GitHub → Actions → Build → Run workflow

查看最近的建置結果：

```bash
gh run list --limit 5
```
