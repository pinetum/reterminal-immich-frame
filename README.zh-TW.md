# reTerminal E1004 — Immich 相片輪播韌體

*[English](README.md) · 繁體中文*

把 Seeed Studio reTerminal E1004（13.3" 全彩 E Ink Spectra 6，1200×1600）變成一個
Immich 數位相框：從指定相簿抓照片、依序或隨機輪播，用機身按鍵手動換圖，並透過
內建網頁介面設定一切。

以**省電**為設計前提：裝置幾乎都在深度睡眠，每次醒來只做一件事就回去睡。

---

## 功能

- 透過 Immich API 讀取指定相簿的照片清單，定時輪播（間隔以分鐘設定）
- 前面板三顆按鍵：下一張 / 上一張 / 重刷；長按 Refresh 開啟設定網頁
- 網頁管理介面：Wi-Fi、Immich 伺服器與 API key、**相簿下拉選單**、輪播間隔、
  隨機播放、影像尺寸、抖動演算法、亮度、方向、裁切方式、低電量門檻等
- 全解析度 1200×1600 的 Floyd–Steinberg 彩色誤差擴散（見下方「為什麼畫質比官方範例好」）
- 照片自動轉向：橫幅照片旋轉成 1600×1200，直幅照片用面板原生方向
- SD 卡影格快取：看過的照片再次顯示時不需連網、不需重新解碼
- 低電量自動暫停，並在螢幕上說明

---

## 硬體

| | |
|---|---|
MCU | ESP32-S3，雙核 Xtensa LX7 @240 MHz
記憶體 | 8 MB OPI PSRAM、32 MB Flash
螢幕 | 13.3" E Ink Spectra 6，1200×1600，6 色（白/綠/紅/黃/藍/黑），全刷約 40 秒
儲存 | microSD，FAT32，64 GB 以下
電池 | 5000 mAh，USB-C 5V/1A 充電
其他 | SHT40 溫濕度、PCF8563 RTC、蜂鳴器、狀態 LED

### 腳位

完整對照表在 [`include/pins.h`](include/pins.h)，重點：

| 功能 | GPIO |
|---|---|
ePaper SPI | SCK 7、MISO 8、MOSI 9、CS 10、CS2 2、DC 11、EN 12、BUSY 13、RST 38
microSD（**與螢幕共用同一條 SPI**） | CS 14、DET 15、EN 16
按鍵（active-LOW，硬體已有 pull-up） | KEY0 `3`、KEY1 `4`、KEY2 `5`
LED（反向邏輯，LOW = 亮） | 48
蜂鳴器 | 45
電池 | ADC 1、enable 21（VBAT = ADC × 2.0）
除錯 UART | TX 43、RX 44 @115200

> **除錯訊息走 `Serial1`（GPIO43/44）**，不是 USB CDC 的 `Serial`。載板的
> USB-to-UART bridge 接在 UART1；USB CDC 需要額外開啟且在這塊板子上不可靠。

---

## 編譯與燒錄

需要 [PlatformIO](https://platformio.org/)。若還沒安裝：

```bash
# 擇一
brew install platformio
pipx install platformio
python3 -m pip install --user platformio
```

然後：

```bash
pio run                      # 編譯（第一次會自動抓所有函式庫）
pio run -t upload            # 燒錄
pio device monitor -b 115200 # 看 log
```

燒錄進不去時：按住 **Boot** 鍵，按一下 **Reset**，放開 Boot，再執行 `pio run -t upload`。

`platformio.ini` 裡有兩個設定**不能改**：

```ini
board_build.arduino.memory_type = qio_opi   ; 沒開 OPI PSRAM，螢幕緩衝區配置會失敗
build_flags = -D BOARD_SCREEN_COMBO=523     ; 選中 E1004 的 T133A01 面板驅動
```

### 已驗證的編譯環境

這份程式碼實際編譯通過（零警告），用的是：

```
PlatformIO Core 6.2.0
espressif32 7.1.3  →  framework-arduinoespressif32 2.0.17
Seeed_GFX 2.0.3        (E1004 的 T133A01 驅動至少要 2.0.3)
JPEGDEC 1.8.4（改過，放在 lib/JPEGDEC，見 PATCHES.md）
PNGdec 1.1.6
ArduinoJson 7.4.3      QRCode 0.0.1
AsyncTCP 3.5.0         ESPAsyncWebServer 3.12.1
```

```
RAM   37.2%  (121 984 / 327 680 bytes)   ← 內部 RAM，不含 8 MB PSRAM
Flash 36.3%  (1 214 797 / 3 342 336 bytes)
```

程式同時相容 Arduino core 2.x 與 3.x：兩者唯一會咬到的差異是 `File::name()`
在 2.x 回傳完整路徑、3.x 只回傳檔名，[`src/sdcard.cpp`](src/sdcard.cpp) 的
`cacheEntryPath()` 兩種都處理。

---

## 硬體自檢（選用，但第一次拿到板子建議跑一次）

專案附了一支獨立的探測程式，會逐一驗證 LED、蜂鳴器、電池 ADC、SD 偵測，並且
**監看 GPIO0–21 的每一次電位變化**：

```bash
pio run -e gpio_probe -t upload
pio device monitor -b 115200
```

然後依序按下五顆按鍵（前面板左／右／Refresh，以及**背面兩顆翻頁鍵**），看 log 印出
哪支腳有動作。

這麼做是因為：Seeed 的文件只說明 KEY0–KEY2 是 GPIO3/4/5 且對應**前面板**三顆鍵，
**沒有寫背面兩顆翻頁鍵接到哪裡**（很可能與 KEY0/KEY1 並聯，但那是推測）。如果探測
結果顯示背面鍵在其他腳位，把它補進 [`include/pins.h`](include/pins.h) 以及
[`src/power.cpp`](src/power.cpp) 裡 `esp_sleep_enable_ext1_wakeup()` 的喚醒遮罩即可。

這支程式不用任何函式庫，幾秒就編譯完。跑完之後用 `pio run -t upload` 燒回正式韌體。

---

## 第一次設定

1. 插入一張 FAT32 格式的 microSD 卡（64 GB 以下），接上 USB-C
2. 燒錄後首次開機，裝置沒有設定，會自己開啟設定模式並在螢幕顯示：
   - Wi-Fi 名稱 `reTerminal-E1004-XXXX`、密碼 `immich1004`
   - 網址 `http://192.168.4.1` 以及一個 **QR code**
   （畫面要等約 40 秒才出現 — 這是 E Ink 全刷的時間）
3. 手機連上那個 Wi-Fi，掃 QR code 或開 `http://192.168.4.1`
4. 在網頁上按 **Scan** 選自家 Wi-Fi、填密碼
5. 填 Immich 伺服器網址（例 `http://192.168.1.50:2283`，不用加 `/api`）與 API key
   （Immich → Account Settings → API Keys）
6. 按 **Save & reconnect**。裝置會重開並連上你家的網路，然後**自動回到設定頁**
7. 這時按 **Test connection** 應該成功，按 **Load albums** 就能從下拉選單挑相簿
8. 設定輪播間隔，按 **Save & resume slideshow**

之後要再進設定頁：**長按前面板 Refresh 鍵 2 秒**（會嗶嗶叫），螢幕會顯示網址與
QR code，預設開啟 10 分鐘。

---

## 按鍵

| 按鍵 | GPIO | 睡眠中按下 | 設定模式中 |
|---|---|---|---|
KEY0（右） | 3 | 下一張 | — |
KEY1（左） | 4 | 上一張 | — |
KEY2（Refresh） | 5 | 短按：重刷目前這張<br>**長按 2 秒：開啟設定頁** | 長按 2 秒：離開設定頁 |

> 按下按鍵後約 40 秒才會看到新照片 — E Ink Spectra 6 沒有局部刷新模式，全彩
> 更新就是這個時間。按下去會立刻嗶一聲、LED 亮起，表示有收到。

---

## 續航

一次換圖大約 50–60 秒在線（Wi-Fi 連線 3 s + 下載 2 s + 解碼與抖動約 10 s +
面板刷新 40 s），約 **2.5 mAh**。

| 輪播間隔 | 每日用量 | 5000 mAh 大約可撐 |
|---|---|---|
10 分鐘 | ~360 mAh | 2 週
1 小時 | ~60 mAh | 2–3 個月
6 小時 | ~10 mAh | 半年以上

幾個省電的實作細節：下載完成後**立刻關掉 Wi-Fi**（解碼與刷新都不需要網路）；
命中 SD 快取時完全不開 Wi-Fi；電量低於門檻時只畫一次警告畫面，之後每小時
靜靜醒來檢查，不再重畫。

---

## 為什麼畫質比官方範例好

Seeed 官方的 E1004 圖片管線（`Seeed_GFX/examples/.../reTerminal_E1004_SDcard_Color6`）
一次處理整張圖：需要 `width×height` 的索引緩衝，誤差擴散還需要
`width×height×3×int16` 的誤差緩衝。在 1200×1600 下那個誤差緩衝就要約 **11 MB**，
8 MB 模組配置不出來，所以 `dither_image()` 會**靜默降級成完全不抖動**——官方範例
自己的註解就寫明了這點，預設因此只能用 Bayer8 有序抖動。

這份韌體改成**逐「目標列」串流**處理（[`src/e6_dither.cpp`](src/e6_dither.cpp)、
[`src/render.cpp`](src/render.cpp)）：只保留 **3 列**誤差（最深的 Jarvis/Atkinson
核心會擴散到 dy=+2），約 **29 KB** 而不是 11 MB。所以全解析度也能跑真正的
Floyd–Steinberg。

記憶體峰值大致是：

```
解碼後的來源影像 (RGB565)   ≤ 4.0 MB
自家 packed-4bpp 影格            0.94 MB
Seeed_GFX 的面板 sprite          0.94 MB
誤差緩衝 3 列 + 列暫存           ~0.04 MB
                            ------------
                             約 6 MB / 8 MB
```

來源用 RGB565 而非 RGB888 是刻意的：最大那塊配置直接少一半，而面板只有 6 種
顏色，5/6/5 的量化在抖動後完全看不出來。

JPEG 解碼用 JPEGDEC 的 1/2、1/4、1/8 **解碼期縮放**，程式會先讀 header 再挑
「在預算內畫質最好」的倍率。這是為什麼即使把影像尺寸設成 `original` 去抓一張
48 MP 的原檔，也不會 OOM 重開機。

JPEGDEC 是**改過的版本**，放在 `lib/JPEGDEC/`（所以 `platformio.ini` 的
`lib_deps` 裡沒有它）。官方版不吃 Immich 產生的 preview：Immich 的 sharp 接的是
mozjpeg，會用三張 AC Huffman 表，於是 header 寫成 SOF1（extended sequential）而
不是 SOF0，官方版在 `open()` 就回 `JPEG_UNSUPPORTED_FEATURE`。三個 patch 的內容
與理由寫在 [`lib/JPEGDEC/PATCHES.md`](lib/JPEGDEC/PATCHES.md)，
`test/host/test_jpeg.cpp` 會驗證它。

解碼前 `decode_jpeg.cpp` 會先自己掃一遍 marker，用與 JPEGDEC 完全相同的接受條件
判斷，所以失敗時 log 印的是真正的原因（哪個 SOF、哪張 Huffman 表），而不是一句
猜測。同一套判斷的電腦版是 `tools/jpegprobe.py`。

---

## 設定項目

網頁上每一項都有說明，這裡只列幾個值得注意的：

| 項目 | 說明 |
|---|---|
**Image size** | 向 Immich 要哪一種尺寸。預設 `preview`（長邊 1440 px）最合適。`original` 畫質最好但最慢，且可能觸發解碼期縮放。 |
**Orientation** | `Automatic` 會把橫幅照片轉成 1600×1200，並假設你是把相框**順時針**轉成橫放。如果照片上下顛倒，就選另一個 landscape 選項。 |
**Dithering** | `Floyd-Steinberg` 是推薦值。`Jarvis` 更平滑但更慢，`Atkinson` 對比更強（它刻意丟掉 2/8 的誤差）。 |
**Brightness (gamma)** | 大於 1.0 變亮。照片在面板上看起來糊糊暗暗時可以試 1.2。 |
**Cached frames** | 每張 960 KB。看過的照片再顯示時不用連網也不用解碼，對按鍵換圖體驗差很多。 |

秘密欄位（Wi-Fi 密碼、API key、管理密碼）**不會**被送回瀏覽器，欄位留空就是
「保留原本的值」。

---

## 測試

純計算的部分（抖動器、縮放/旋轉/打包）可以直接在電腦上測，不需要裝置也不需要
PlatformIO：

```bash
./test/host/run.sh
```

四組測試：

- `test_stream` — 證明串流式 3 列誤差緩衝與**整張圖**的參考實作
  **逐像素完全相同**（FS / Jarvis / Atkinson 都測）。這是能抓到緩衝區輪替錯誤的那一個。
- `test_dither` — 檢查各演算法的**顏色重建精度**。注意：Spectra 6 沒有灰色，
  中灰的最近色其實是「綠色」，所以正確的檢驗標準是*區域平均還原出的 RGB*要接近
  輸入，而不是黑白點的比例。實測 FS 的平均誤差在 0.4–5.0（滿刻度 255）。
- `test_render` — 四種旋轉的座標映射、cover 必須完全填滿、contain 的留白比例、
  以及 4bpp 的 nibble 打包順序。
- `test_jpeg` — 證明 `lib/JPEGDEC` 的 patch 真的有效：內嵌一張 baseline JPEG，與
  「同一份 entropy data、header 改寫成 SOF1 + 第三張 AC Huffman 表」的版本，兩者
  必須解出逐像素完全相同的結果。沒有 patch 的話後者在 `open()` 就會失敗。

`test/host/Arduino.h` 是一個極簡的 Arduino API 替身，只為了讓那兩個檔案能在
桌機上編譯。

---

## 發佈 release

```bash
./tools/package-release.sh v1.0.0
```

它會編譯並把 release 需要的東西全部放進 `dist/`：四個燒錄映像、一個從 0x0
開始的**合併映像**（這樣沒裝過 PlatformIO 的人只要一行 esptool 就能燒，也可以
直接餵給 ESP Web Tools）、用來還原 panic backtrace 的 ELF、`SHA256SUMS`，以及
寫明各段偏移的 `FLASHING.md`。

ESP32-S3 的 bootloader 在 **0x0**，不是原始 ESP32 的 0x1000。腳本裡的偏移是從
build 環境讀出來的，不是憑印象寫死的（`pio run -t envdump | grep -A6 FLASH_EXTRA_IMAGES`）。

接著用 [GitHub CLI](https://cli.github.com/)：

```bash
gh release create v1.0.0 dist/* --title "v1.0.0" --notes-file dist/FLASHING.md
```

`dist/` 已經在 .gitignore 裡。

---

## 架構

```
src/
  main.cpp       省電狀態機：為什麼醒來 → 做一件事 → 回去睡
  app.cpp        統籌「顯示某張照片」的完整流程，以及網頁動作佇列
  power.cpp      電池、LED、蜂鳴器、長按偵測、深度睡眠（timer + ext1 按鍵喚醒）
  display.cpp    EPaper 初始化與刷新；狀態、錯誤、設定（含 QR code）畫面
  sdcard.cpp     SPI 共用的掛載順序、影格快取與 LRU 淘汰
  net.cpp        Wi-Fi STA / SoftAP + captive portal
  immich.cpp     相簿列表、播放清單抓取、照片串流下載
  playlist.cpp   SD 上的播放清單、游標、隨機播放
  decode.cpp     格式嗅探（看 magic bytes，不看副檔名）、共用檔案存取、記憶體配置
  decode_jpeg.cpp  header 探測 + JPEGDEC，含解碼期縮放保護 → PSRAM RGB565
  decode_png.cpp   PNGdec → PSRAM RGB565
  render.cpp     串流式縮放 + 旋轉 + 抖動 → packed 4bpp
  e6_dither.cpp  Spectra 6 調色盤與串流式誤差擴散
  webui.cpp      非阻塞的管理 API
  web_assets.h   管理頁（HTML/CSS/JS，放在 PROGMEM）
  tools/
    gpio_probe.cpp  獨立的硬體自檢（另一個 PlatformIO 環境，不會編進韌體）
include/         pins.h、settings.h、e6_dither.h、log.h
test/host/       桌機測試
lib/JPEGDEC/     改過的 JPEGDEC（見 PATCHES.md）
tools/
  jpegprobe.py   桌機版的 JPEG header 檢查，與韌體內的判斷同一套
```

幾個不明顯但重要的設計：

**SD 卡與螢幕共用一條 SPI。** 必須先 `epaper.begin()`，然後重用螢幕驅動建立的
`SPIClass` 實例來 `SD.begin()`（見 [`src/sdcard.cpp`](src/sdcard.cpp)）。自己另開
一個 SPIClass 會跟螢幕搶匯流排，這是這塊板子上 SD 掛不起來最常見的原因。
刷新面板的那 40 秒期間也絕對不能碰 SD。

**網頁處理函式裡不做任何慢事。** ESPAsyncWebServer 的 handler 跑在 AsyncTCP 的
task 上，而那個 task 同時負責 lwIP 的 callback——在裡面做阻塞的 socket 呼叫
（連 Immich、掃 Wi-Fi）可能把網路堆疊鎖死，碰 SD 卡則會跟主 task 競爭。所以
每個 handler 只是把意圖記在 `g_status.action` 就回傳 202，實際工作由 `main.cpp`
的 `loop()` 執行，網頁再輪詢 `/api/status`。

**兩個解碼器必須分開編譯單元。** JPEGDEC 與 PNGdec 都定義了同名但內容不同的
輔助巨集（`INTELSHORT` / `MOTOSHORT` / `MOTOLONG` …），同時 include 到一個 .cpp
會產生一堆重定義警告。所以拆成 `decode_jpeg.cpp` 與 `decode_png.cpp`，
[`src/decode_internal.h`](src/decode_internal.h) 只放兩者共用的那一點東西。

**旋轉不用 `setRotation()`。** 旋轉做在「目標座標 → 來源座標」的映射裡，
是個零成本的索引變換，而且誤差擴散仍然沿著**目標**掃描順序進行——那才是眼睛
閱讀影像的順序，也才是正確的做法。

---

## Immich API 的一個陷阱

照片清單走 `POST /api/search/metadata`（body 帶 `albumIds`），**不是**
`GET /api/albums/{id}`。原因：新版 Immich 的 `AlbumResponseDto` 已經沒有 `assets`
陣列了，只剩 `assetCount`。

`albumIds` / `page` / `type` / `order` 這幾個欄位從 Immich v3.2 起被標記為
deprecated，但仍然可用，而且是目前唯一能同時相容 v1.9x 與 v3.x 的寫法。
若未來 v4 真的移除，改用 cursor 分頁（`query.cursor` / `nextCursor`）即可——
[`src/immich.cpp`](src/immich.cpp) 裡有註解標示位置。

---

## 故障排除

| 現象 | 檢查 |
|---|---|
螢幕完全沒反應，log 說 `framebuffer not allocated` | `board_build.arduino.memory_type = qio_opi` 沒設到；`pio run -t clean` 後重編 |
log 說 `BUSY stuck low` | 面板沒回應：檢查排線與電源 |
`SD.begin FAILED` | 卡要 FAT32、64 GB 以下；確認有插好；log 會印出 SD_DET 的讀值 |
照片顯示 `WEBP NOT SUPPORTED` | Immich 的縮圖格式設成了 WebP。改成 JPEG：Administration → Settings → Image Settings |
照片顯示 `DECODE FAILED` | log 會印出確切原因（`[dec] cannot decode: ...`）。多半是 progressive JPEG：Immich 的 Administration → Settings → Image Settings 把 **Progressive** 關掉，再跑一次 Generate Thumbnails。想先用電腦確認，把圖抓下來跑 `python3 tools/jpegprobe.py <file.jpg>` |
照片顯示 `IMAGE TOO LARGE` | 影像尺寸改成 `preview`（PNG 沒有解碼期縮放可用） |
`HTTP 401` / `API key rejected` | API key 錯了，或貼到了多餘的空白 |
`HTTP 404` | 伺服器網址或相簿 UUID 錯了。網址只要 origin，不要加 `/api` |
橫幅照片上下顛倒 | Orientation 選另一個 landscape 選項 |
設定頁連不上 | 設定視窗（預設 10 分鐘）已經過期，長按 Refresh 2 秒重開 |
每張照片都要等很久 | 正常，E Ink Spectra 6 全刷約 40 秒，沒有局部刷新 |

把 log 接出來看最快：`pio device monitor -b 115200`。每個階段都會印記憶體用量，
OOM 會以趨勢的方式顯示出來，而不是突然重開機。

---

## 已知限制與取捨

1. **40 秒的面板全刷是硬體限制。** Spectra 6 沒有局部刷新，按鍵換圖必然要等。
   SD 影格快取省掉下載與解碼（約 12 秒），但省不掉刷新。
2. **設定網頁不是隨時可用。** 這是省電模式的直接代價。用「長按 Refresh 進設定」
   加上「螢幕直接顯示網址與 QR code」來補足。
3. **不支援 WebP 與 progressive JPEG。** Immich 的縮圖格式預設是 JPEG，但可以被
   改成 WebP；preview 也有一個 Progressive 開關。兩者遇到時都會顯示明確的錯誤與
   修正指引，而不是靜默失敗。baseline 與 SOF1（extended sequential）都支援。
4. **HTTPS 預設不驗證憑證**（`setInsecure()`）。家用自簽環境的務實選擇；裝置上
   沒有憑證信任庫。要啟用驗證可以在設定裡貼入 CA PEM（`caPem` 欄位）。
5. **螢幕上的文字只能是 ASCII。** Seeed_GFX 內建的字型沒有中日韓字符，所以機身
   畫面（錯誤、設定、低電量）都是英文。網頁介面不受這個限制。
6. **背面翻頁鍵的 GPIO 尚未查證。** Seeed wiki 只說明 KEY0–KEY2 對應*前面板*三鍵，
   沒有寫背面兩顆接到哪裡（很可能與 KEY0/KEY1 並聯）。目前程式只處理 GPIO 3/4/5。
   若背面鍵是獨立 GPIO，要補進 `include/pins.h` 與 `src/power.cpp` 的
   `esp_sleep_enable_ext1_wakeup()` 遮罩。
7. **沒有牆上時鐘。** 沒做 NTP，播放清單的「年齡」是靠累加睡眠時間估算的，用來
   讓每日 TTL 到期足夠準確。

---

## 參考

- [reTerminal E1004 入門](https://wiki.seeedstudio.com/getting_started_with_reterminal_e1004/)
- [Arduino Cookbook: Onboard Peripherals（按鍵、LED、電池、SD 的腳位來源）](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino_peripherals_2/)
- [Seeed_GFX](https://github.com/Seeed-Studio/Seeed_GFX) — `EPaper` 類別與 `Setup523`
- [Immich API 文件](https://api.immich.app/endpoints)
