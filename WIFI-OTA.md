# WiFi OTA 韌體更新

本文件說明 ESP32-S3 掌機的 WiFi OTA（Over-The-Air）韌體更新機制，包含使用方式、運作原理與故障排除。

---

## 快速使用

### 1. 準備設定檔

在 SD 卡建立 `system/wifi.cfg`：

```
SSID=你的WiFi名稱
PASS=你的WiFi密碼
OTA_URL=http://your-server/firmware.bin
```

> 支援 Windows (`\r\n`) 與 Unix (`\n`) 換行格式。

### 2. 執行更新

1. 進入 **⚙ Settings**
2. 選擇 **WiFi OTA** → 按 **A**
3. 畫面依序顯示：
   - `Connecting WiFi...` — 正在連線
   - `Downloading... X%` — 正在下載
   - 下載完成後**自動重新啟動**，載入新韌體

---

## 運作流程

```
使用者觸發 OTA
      │
      ▼
① 讀取 /sdcard/system/wifi.cfg
  解析 SSID / PASS / OTA_URL
      │
      ▼
② WiFi 連線 (STA 模式)
  ├─ 最多重試 5 次
  ├─ 逾時 15 秒
  └─ 取得 IP 後繼續
      │
      ▼
③ HTTP 下載韌體
  ├─ 建立 TCP 連線至 OTA_URL
  ├─ 取得 Content-Length 驗證非零
  └─ 迴圈讀取，每次 4KB
      │
      ▼
④ 寫入備用 OTA 分區
  ├─ 自動選擇非當前運行的分區
  ├─ 清除目標分區
  ├─ 逐塊寫入 Flash
  └─ 更新進度百分比
      │
      ▼
⑤ 驗證韌體完整性
  ├─ 計算並比對韌體 hash
  └─ 設定下次開機分區
      │
      ▼
⑥ 自動重新啟動
      │
      ▼
⑦ Rollback（若新韌體異常）
  bootloader 偵測未確認的分區
  自動回滾到舊韌體
```

---

## 雙 OTA 分區機制

Flash 採用 A/B 雙分區設計，每個分區 **7.5MB**：

```
Flash (16MB)
┌────────────┐ 0x009000  NVS (24KB)
├────────────┤ 0x00F000  OTA Data (8KB) ← 記錄目前開機分區
├────────────┤ 0x011000  PHY Init (4KB)
├────────────┤ 0x020000
│   ota_0   │           7.5MB ← 第一次燒錄於此
├────────────┤ 0x7A0000
│   ota_1   │           7.5MB ← OTA 下載寫入此處
├────────────┤ 0xF20000
│  storage  │           896KB (SPIFFS)
└────────────┘
```

- OTA 下載時，韌體永遠寫入**非當前運行**的分區
- 下次更新方向反轉：`ota_1` 運行時寫入 `ota_0`
- 若中途斷電或下載失敗，當前運行分區**完全不受影響**

---

## Rollback（回滾）機制

本專案啟用了 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`。

```
新韌體首次開機
      │
      ▼
app 需呼叫 esp_ota_mark_app_valid_cancel_rollback()
      │
   ┌──┴──┐
   │     │
 成功   未呼叫 / 崩潰
   │     │
   ▼     ▼
繼續  下次開機 bootloader 偵測分區未標記為 valid
運行  自動回滾到上一個正常分區
```

| 情況 | 結果 |
|------|------|
| 新韌體正常啟動並標記 valid | 正常使用新版本 |
| 新韌體啟動後崩潰/重啟 | 自動回滾到舊版本 |
| 下載中途斷電 | 舊分區未動，重開機直接用舊版本 |
| HTTP 連線失敗 | 不寫入任何資料，繼續用舊版本 |

---

## 相關設定常數

定義於 `include/config.h`：

| 常數 | 值 | 說明 |
|------|----|------|
| `WIFI_CONFIG_PATH` | `/sdcard/system/wifi.cfg` | 設定檔路徑 |
| `WIFI_MAX_RETRY` | 5 | WiFi 最大重試次數 |
| `WIFI_CONNECT_TIMEOUT_MS` | 15000 | 連線逾時（毫秒） |

定義於 `src/ota_update.c`：

| 常數 | 值 | 說明 |
|------|----|------|
| `OTA_BUF_SIZE` | 4096 | 每次 HTTP 讀取大小（bytes） |
| `timeout_ms` | 30000 | HTTP 連線逾時（毫秒） |

---

## 注意事項

- **電池**：更新過程中請確保電量充足（建議 > 50%），OTA 寫入 Flash 期間若斷電可能造成分區損壞
- **ADC2 衝突**：WiFi 啟用期間電池電量讀數會暫時凍結，這是正常現象
- **僅支援 HTTP**：目前不支援 HTTPS；OTA_URL 請使用內網伺服器或受信任的網路環境
- **韌體格式**：OTA_URL 必須指向由 PlatformIO 編譯產生的 `.bin` 檔案（位於 `.pio/build/esp32s3/firmware.bin`）

---

## 故障排除

| 錯誤訊息 | 原因 | 解決方式 |
|---------|------|---------|
| `Cannot open wifi.cfg` | 設定檔不存在 | 確認 SD 卡有 `system/wifi.cfg` |
| `No SSID found` | wifi.cfg 缺少 SSID= 行 | 檢查設定檔格式 |
| `WiFi connection failed` | 密碼錯誤或訊號弱 | 確認 SSID/PASS，移近路由器 |
| `Invalid content length` | URL 無效或伺服器無回應 | 確認 OTA_URL 可正常下載 |
| `No OTA partition available` | 分區表設定有誤 | 確認使用正確的 `partitions.csv` |
| `HTTP read error` | 下載中途斷線 | 確認網路穩定後重試 |
| `esp_ota_end failed` | 韌體 hash 不符 | 韌體檔案損壞，重新產生 |

---

## 產生韌體檔案

```bash
# 編譯
pio run

# 韌體位置
.pio/build/esp32s3/firmware.bin
```

將 `firmware.bin` 放到可透過 HTTP 存取的伺服器，填入 `OTA_URL` 即可。
