# 硬體接線說明

本文件說明 ESP32-S3-DevKitC-1-N16R8 與各周邊模組的完整接線方式。

## 元件清單

| 元件 | 型號 |
|------|------|
| MCU 開發板 | ESP32-S3-DevKitC-1-N16R8 (16MB Flash / 8MB PSRAM) |
| 螢幕 | 2.2" ILI9341 TFT LCD (240×320, SPI 介面) |
| 音訊放大器 | MAX98357A I2S DAC + 8Ω 喇叭 |
| 儲存 | Micro SD 卡模組 (SPI 模式) |
| 按鍵 | 輕觸按鈕 ×10 |
| 電池 | 3.7V 鋰電池 + 分壓電路 |

---

## 1. LCD 螢幕 (ILI9341) — SPI2

| ILI9341 腳位 | 說明 | ESP32-S3 GPIO |
|-------------|------|--------------|
| VCC | 電源 3.3V | 3.3V |
| GND | 接地 | GND |
| MOSI (SDA) | SPI 資料輸出 | GPIO 11 |
| MISO | SPI 資料輸入 | GPIO 13 |
| SCK (CLK) | SPI 時脈 | GPIO 12 |
| CS | 晶片選擇 | GPIO 10 |
| DC (RS) | 資料/指令切換 | GPIO 9 |
| RST | 硬體重置 | GPIO 14 |
| LED (BL) | 背光 | 3.3V (常亮) |

> SPI2 時脈頻率：40 MHz

```
ESP32-S3          ILI9341
GPIO11  ────────  MOSI
GPIO13  ────────  MISO
GPIO12  ────────  SCK
GPIO10  ────────  CS
GPIO9   ────────  DC
GPIO14  ────────  RST
3.3V    ────────  VCC
3.3V    ────────  LED
GND     ────────  GND
```

---

## 2. SD 卡模組 — SPI3

| SD 模組腳位 | 說明 | ESP32-S3 GPIO |
|-----------|------|--------------|
| VCC | 電源 3.3V | 3.3V |
| GND | 接地 | GND |
| MOSI | SPI 資料輸出 | GPIO 35 |
| MISO | SPI 資料輸入 | GPIO 37 |
| SCK | SPI 時脈 | GPIO 36 |
| CS | 晶片選擇 | GPIO 38 |

```
ESP32-S3          SD 模組
GPIO35  ────────  MOSI
GPIO37  ────────  MISO
GPIO36  ────────  SCK
GPIO38  ────────  CS
3.3V    ────────  VCC
GND     ────────  GND
```

---

## 3. 音訊放大器 (MAX98357A) — I2S

| MAX98357A 腳位 | 說明 | ESP32-S3 GPIO |
|--------------|------|--------------|
| VIN | 電源 3.3V–5V | 3.3V |
| GND | 接地 | GND |
| BCLK | I2S 位元時脈 | GPIO 17 |
| LRC (LRCLK) | I2S 左右聲道時脈 | GPIO 18 |
| DIN | I2S 資料輸入 | GPIO 16 |
| GAIN | 增益設定 | 懸空 (預設 9dB) |
| SD | 關機控制 | 懸空 (預設開啟) |
| 喇叭+ | 輸出正極 | 8Ω 喇叭 |
| 喇叭- | 輸出負極 | 8Ω 喇叭 |

> 取樣率：22050 Hz，單聲道

```
ESP32-S3          MAX98357A
GPIO17  ────────  BCLK
GPIO18  ────────  LRC
GPIO16  ────────  DIN
3.3V    ────────  VIN
GND     ────────  GND

MAX98357A         喇叭
OUT+    ────────  喇叭(+)
OUT-    ────────  喇叭(-)
```

---

## 4. 按鍵輸入

所有按鍵一端接 GPIO，另一端接 GND。GPIO 內部已啟用上拉電阻，按下時為低電位 (Active Low)。

| 按鍵 | ESP32-S3 GPIO |
|------|--------------|
| 上 (UP) | GPIO 1 |
| 下 (DOWN) | GPIO 2 |
| 左 (LEFT) | GPIO 3 |
| 右 (RIGHT) | GPIO 4 |
| A | GPIO 5 |
| B | GPIO 6 |
| START | GPIO 7 |
| SELECT | GPIO 8 |
| 音量+ (VOL+) | GPIO 21 |
| 音量- (VOL-) | GPIO 47 |

```
ESP32-S3          按鍵
GPIO1   ────┤├──  GND   (UP)
GPIO2   ────┤├──  GND   (DOWN)
GPIO3   ────┤├──  GND   (LEFT)
GPIO4   ────┤├──  GND   (RIGHT)
GPIO5   ────┤├──  GND   (A)
GPIO6   ────┤├──  GND   (B)
GPIO7   ────┤├──  GND   (START)
GPIO8   ────┤├──  GND   (SELECT)
GPIO21  ────┤├──  GND   (VOL+)
GPIO47  ────┤├──  GND   (VOL-)
```

---

## 5. 電池電壓偵測

鋰電池最高電壓 4.2V，超過 ESP32-S3 ADC 量測上限，因此需透過**分壓電路**將電壓減半後送入 GPIO15。

### 分壓電路接線

```
電池(+) ──── R1(100kΩ) ────┬──── R2(100kΩ) ──── GND
                           │
                        GPIO15 (ADC2_CH4)
```

| 節點 | 電壓範圍 |
|------|---------|
| 電池實際電壓 | 3.3V（沒電）~ 4.2V（滿電） |
| GPIO15 讀取電壓 | 1.65V ~ 2.1V |

> 使用相同阻值的 R1、R2（例如各 100kΩ），分壓比為 1:1，程式內乘以 2 還原實際電壓。

> **注意：** GPIO15 使用 ADC2，與 WiFi 共用資源。啟用 WiFi 期間 ADC2 無法正常讀取，韌體會自動暫停電池量測。

---

## 6. GPIO 總覽

| GPIO | 功能 | 說明 |
|------|------|------|
| 1 | 按鍵 UP | 上拉輸入 |
| 2 | 按鍵 DOWN | 上拉輸入 |
| 3 | 按鍵 LEFT | 上拉輸入 |
| 4 | 按鍵 RIGHT | 上拉輸入 |
| 5 | 按鍵 A | 上拉輸入 |
| 6 | 按鍵 B | 上拉輸入 |
| 7 | 按鍵 START | 上拉輸入 |
| 8 | 按鍵 SELECT | 上拉輸入 |
| 9 | LCD DC | 數位輸出 |
| 10 | LCD CS | SPI2 CS |
| 11 | LCD MOSI | SPI2 MOSI |
| 12 | LCD SCK | SPI2 CLK |
| 13 | LCD MISO | SPI2 MISO |
| 14 | LCD RST | 數位輸出 |
| 15 | 電池 ADC | ADC2_CH4 |
| 16 | I2S DOUT | I2S 資料 |
| 17 | I2S BCK | I2S 位元時脈 |
| 18 | I2S LRCK | I2S 聲道時脈 |
| 21 | 按鍵 VOL+ | 上拉輸入 |
| 35 | SD MOSI | SPI3 MOSI |
| 36 | SD SCK | SPI3 CLK |
| 37 | SD MISO | SPI3 MISO |
| 38 | SD CS | SPI3 CS |
| 47 | 按鍵 VOL- | 上拉輸入 |

---

## 注意事項

- 所有模組使用 **3.3V** 供電，勿接 5V（ESP32-S3 IO 不耐 5V）
- LCD 與 SD 卡各自使用獨立 SPI 匯流排（SPI2 / SPI3），不互相干擾
- 電池分壓電阻建議使用 100kΩ，降低靜態功耗
- GPIO15 (ADC2) 與 WiFi 不可同時使用，韌體已自動處理切換
