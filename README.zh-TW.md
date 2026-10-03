# reTerminal E1004 — Immich 相片輪播韌體

*[English](README.md) · 繁體中文*

把 Seeed Studio reTerminal E1004（13.3" 全彩 E Ink Spectra 6，1200×1600）變成一個
Immich 數位相框：從指定相簿抓照片、依序或隨機輪播，用機身按鍵手動換圖，並透過
內建網頁介面設定一切。

以**省電**為設計前提：裝置幾乎都在深度睡眠，每次醒來只做一件事就回去睡。

> ### ⚡ [用瀏覽器直接燒錄](https://pinetum.github.io/reterminal-immich-frame/)
>
> <https://pinetum.github.io/reterminal-immich-frame/> —— 板子插上 USB-C，按一個按鈕就好。
> 不用裝 PlatformIO，不用工具鏈。需要桌機版的 Chrome / Edge / Opera（Web Serial）。

---

## 功能

- 透過 Immich API 讀取指定相簿的照片清單，定時輪播（間隔以分鐘設定）
- 前面板三顆按鍵：下一張 / 上一張 / 重刷；長按 Refresh 開啟設定網頁
- 網頁管理介面：Wi-Fi、Immich 伺服器與 API key、**相簿下拉選單**、輪播間隔、
  隨機播放、影像尺寸、方向、裁切方式、低電量門檻，以及下面整套影像管線
- 全解析度 1200×1600 的 Floyd–Steinberg 彩色誤差擴散（見下方「為什麼畫質比官方範例好」）
- **校準調色盤**：用面板「真正顯示得出來」的顏色去抖色，而不是名義上那組飽和
  RGB；提供六格可自行輸入的 hex，以及在面板上畫測試色塊圖供自行量測
- **十二種誤差擴散核心**、有序（Bayer）與隨機抖色、蛇行掃描，以及
  RGB / LAB / chroma 三種色彩匹配距離
- **抖色前的影像預處理**：曝光、飽和度、對比或 S 曲線色調映射、把動態範圍壓進
  面板實際範圍、清晰度（局部對比）與暖紙中和，共七組預設
- **瀏覽器預覽**：幾秒鐘就能看到某個設定的效果，不必等 40 秒的面板刷新
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

只有要改程式才需要這一段。單純想裝韌體的話，用[瀏覽器燒錄頁](https://pinetum.github.io/reterminal-immich-frame/)，
或是任一個 [release](https://github.com/pinetum/reterminal-immich-frame/releases) 附的映像檔。

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
RAM   40.7%  (133 400 / 327 680 bytes)   ← 內部 RAM，不含 8 MB PSRAM
Flash 37.7%  (1 259 553 / 3 342 336 bytes)
```

校準調色盤這批功能佔其中約 11 KB RAM 與 45 KB flash。RAM 幾乎全是
[`src/colorspace.cpp`](src/colorspace.cpp) 裡 sRGB↔linear 與立方根的查表，它取代
每張影格約 580 萬次的 `powf`/`cbrtf`；之所以用靜態配置而不是動態配置，是因為這樣
就不可能在要用的時候配置失敗。flash 則是新程式碼加上約 17 KB 的管理網頁。

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

### 第二個原因：調色盤本來就是錯的

上面講的都是「怎麼負擔得起誤差擴散」。另外值得單獨說明的是：在此之前這份韌體
跟 Seeed 範例一樣，是對著錯誤的顏色在擴散誤差。

E Ink Spectra 6 面板的六色，習慣上用「你希望它是」的飽和 RGB 來描述——白是
`#FFFFFF`、紅是 `#FF0000`。面板做不出那些顏色。實機量測下，它的白是大約
`#B9C7C9` 的淺灰（亮度只比紙白的一半多一點），綠則是很暗的 `#35563A`。aitjcize
用色度計量的結果是：白比理論值暗 30%、綠暗 73%。

所以拿飽和值去做最近色搜尋，等於在回答錯的問題；而對誤差擴散來說更糟的是，傳給
鄰近像素的誤差是對著一個面板永遠不會顯示的顏色算出來的，於是那些鄰居會去「修正」
一個根本不存在的偏差。解法是：**匹配與擴散都對校準色，輸出仍然是裝置自己的色碼**，
也就是下面「校準調色盤與影像處理」要講的東西。

解碼前 `decode_jpeg.cpp` 會先自己掃一遍 marker，用與 JPEGDEC 完全相同的接受條件
判斷，所以失敗時 log 印的是真正的原因（哪個 SOF、哪張 Huffman 表），而不是一句
猜測。同一套判斷的電腦版是 `tools/jpegprobe.py`。

---

## 校準調色盤與影像處理

移植自兩個把這件事做得很好的專案：
[paperlesspaper/epdoptimize](https://github.com/paperlesspaper/epdoptimize)（給
電子紙用的 JavaScript 抖色函式庫與互動工具）與
[aitjcize/esp32-photoframe](https://github.com/aitjcize/esp32-photoframe)（同一套
想法在 ESP32-S3 上用 C 實作），以及後者的
[epaper-image-convert](https://github.com/aitjcize/epaper-image-convert)。

**這裡的所有功能預設都是關的。** 從舊版升級上來的機器，畫出來的影格與升級前
完全一致（bit 級一致）——`test_dither` 就是拿舊韌體跑出來的數字在斷言這件事。
要不要打開，是你自己的決定。

### 匹配用校準色，輸出用裝置色

整條管線的核心想法，兩個參考專案各自獨立得到同樣的結論：

```
最近色搜尋     對著「校準色」       （面板真正顯示的顏色）
擴散的誤差     對著「校準色」
寫進面板的位元組                    「裝置色碼」
```

epdoptimize 最後那一步需要另外跑一次 `replaceColors()`，因為它處理的是 RGB
canvas。我們不需要：這份韌體的抖色器本來就直接輸出 4-bit 色碼，而色碼**就是**
裝置色。

### 調色盤

| 選項 | 數值 | 來源 |
|---|---|---|
`理論值（飽和）` | `#FFFFFF #1DB954 #E53935 #FFD800 #004CFF #000000` | Seeed_GFX `dither.cpp` —— **預設值**，也是這份韌體一直以來用的
`Spectra 6 實測` | `#B9C7C9 #35563A #62201E #C1BB1E #233F8E #1F2226` | epdoptimize `spectra6`，它推薦的校準值
`…legacy` | `#E8E8E8 #125F20 #B21318 #EFDE44 #2157BA #191E21` | epdoptimize `spectra6legacy`
`…Boeber` | `#D6D6D6 #067406 #EA4843 #DBD529 #416CE1 #1F2226` | epdoptimize `spectra6-boeber`
`…aitjcize` | `#BEC8C8 #27663C #871300 #CDCA00 #05409E #020202` | epdoptimize `aitjcize-spectra6`，與 esp32-photoframe 的預設值相同
`自訂` | 六格可編輯 hex，存在 NVS | 你自己的面板

Spectra 6 不同批次會有差異，所以沒有哪一組實測值對每片面板都正確。按
**Draw the test chart** 會把六種墨水畫成實心色塊；在均勻的間接光下（不要閃光燈）
拍下面板，用任何取色器讀出每一塊的值，再填進自訂欄位即可。這是 esp32-photoframe
那套「上傳照片自動取樣」校準精靈的手動版本。

### 抖色

| 控制項 | 選項 |
|---|---|
**方法** | 誤差擴散（推薦）、有序 / Bayer、隨機、只取最近色
**核心** | Floyd–Steinberg、false Floyd–Steinberg、Atkinson、Jarvis–Judice–Ninke、Stucki、Burkes、Sierra-3、Sierra-2、Sierra-2-4A、Fan、Shiau–Fan、Shiau–Fan 2
**蛇行掃描** | 每一列交替掃描方向，可消掉誤差擴散在平坦區域留下的淡淡斜向紋路
**色彩距離** | `RGB`（純歐氏距離）、`LAB`（感知 ΔE）、`chroma`（RGB 再加上懲罰項，避免飽和的粉彩色塌成白色）

有序與隨機抖色沒有誤差回饋，所以在六色調色盤上沒辦法混兩種墨水去做出中間色——
一片平坦的中灰會整片變成單一顏色（而且是綠色，因為 `#808080` 在 Spectra 6 裡
最近的真的就是綠）。它們是為了完整性才放的；照片請用誤差擴散。

### 抖色前的處理

各階段的順序，照著 epdoptimize 的 `applyImageProcessing()` 與
epaper-image-convert 的 `preprocessImage()`：

> gamma → 暖紙中和 → 清晰度 → 曝光 → 飽和度 → 色調（對比或 S 曲線）
> → 動態範圍 → 色階 → 白點保留

七組預設（`balanced`、`dynamic`、`vivid`、`soft`、`grayscale`、`restore`、
`posterScan`）來自 epdoptimize，在匯入時一次換算成 epaper-image-convert 與
esp32-photoframe 的**乘數慣例**：曝光、飽和度、對比的中性值是 `1.0`，高光壓縮
取正值；而不是 epdoptimize 那套「中性 0.0、曝光用級數、高光壓縮取負」的加法慣例。
兩者可以互換，乘數的滑桿範圍比較好用。

**動態範圍壓縮**是最值得理解的一項。面板的白只反射紙白一半多一點的光，所以照片
的完整範圍塞不進去；這一步把亮度重新映射進調色盤自己的黑→白範圍，而不是讓兩端
直接被裁掉。`display` 假設來源填滿 0–100%，`auto` 則先量測這張照片自己的百分位。
另外有一個彩度保護遮罩，讓飽和的像素不會跟中性色一起被壓平。

### 它怎麼塞進串流式算繪器

參考專案處理的 canvas 已經是輸出尺寸，想掃幾遍都可以。我們不行：1200×1600 的
RGB888 中間緩衝要 5.8 MB，而我們手上已經有 960 KB 的影格和好幾 MB 的解碼來源。
esp32-photoframe 撞到同一道牆，解法也一樣——讓所有東西都 row-local。所以
[`src/imgproc.cpp`](src/imgproc.cpp) 把各階段拆成三塊：

- **來源掃描**——gamma、暖紙中和、清晰度，也就是需要 2D 鄰域、或必須在縮放前
  執行的那些階段，就地串流掃過解碼後的來源。清晰度把 epdoptimize 的兩塊整張圖
  暫存換成兩圈 `2·radius+1` 列的環形緩衝，約 78 KB；`test_imgproc` 證明這個滑動
  視窗與整張圖的 box blur 逐像素完全一致。
- **分析**——一次唯讀、降取樣的掃描，把百分位驅動的 `auto` 模式解析成具體數字，
  而且只掃「真的會出現在面板上」的那塊來源，所以 cover 模式裁掉的邊緣不會把
  範圍算歪。
- **逐列處理**——所有逐點運算，在某一列縮放完、要送進抖色器之前才套用。沒有中間
  緩衝，也不會再被量化回 RGB565 一次。

當所有階段都是中性值時（也就是預設），來源掃描會被整個跳過、逐列處理是空操作，
所以沒改過設定的機器完全不需要為此付出任何代價。

### 與參考實作刻意不同的地方

- **動態範圍壓縮在線性亮度域做，不在 Lab。** epdoptimize 是逐像素 Lab 來回換算；
  esp32-photoframe 明確選了亮度縮放，因為它保留色度又避開裝置上的來回換算，而且
  他們的註解記錄了逐通道重映射「會連同亮度一起壓縮彩度、肉眼可見地讓中間調發白」
  因此被改回去。我們跟隨他們，再加上 epdoptimize 的 `auto` 百分位與彩度保護。
  範圍映射另外提供 `CIE L*` 選項。
- **飽和度用「朝 HSL 亮度做線性插值」計算**，這在代數上與參考實作的 HSL 來回換算
  是**同一個函式**，只是把三角運算消掉了，成本約十分之一。`test_imgproc` 拿一份
  逐字照抄的實作來證明這個恆等式，而不是只在註解裡宣稱。
- **`random` 是加雜訊後再匹配調色盤。** epdoptimize 的 `random` 把每個通道硬寫成
  0 或 255 並完全忽略調色盤，這對六色面板沒有意義。
- **暖紙中和只開放模式與強度兩個參數。** 其餘七個常數在 epdoptimize 裡只存在過
  一組調校好的值——`posterScan` 預設——那組就是這裡寫死的值。
- **有序抖色的門檻以零為中心**，epdoptimize 則是加一個只有正值的偏移，會讓整張圖
  變亮。Bayer 矩陣現在改用 epdoptimize 的遞迴建構法，它是這份韌體原本手寫那張表
  的轉置——肉眼分不出來，但有序抖色的輸出與舊版不再是 bit 級一致。

**沒有實作的部分：** 藍雜訊抖色（光是遮罩就是 3.8 MB 的 PNG）、coverage-based
有序抖色、邊緣保留／反鋸齒後處理（這三項都需要整張圖的 pass），以及自動處理建議。

### 預覽

**Save & preview** 會重新算繪目前這張照片並把結果畫在設定頁上，二十幾個新參數
才真的調得動。它的代價是一次解碼加一次算繪（幾秒鐘），而不是 40 秒的面板刷新。

它算繪的是**真正的**全解析度影格，然後把每個 4×4 區塊的色碼透過校準調色盤做
平均，縮成 300×400。這刻意不等於「直接用預覽尺寸去抖色」——那樣顆粒會比實際粗
四倍；對校準色做平均正是人眼在正常觀看距離下做的事，所以這才是誠實的預覽。
重畫之後來源照片會留在 SD 卡上（並標記它的 asset id），所以第二次預覽不需要再下載。

---

## 設定項目

網頁上每一項都有說明，這裡只列幾個值得注意的：

| 項目 | 說明 |
|---|---|
**Image size** | 向 Immich 要哪一種尺寸。預設 `preview`（長邊 1440 px）最合適。`original` 畫質最好但最慢，且可能觸發解碼期縮放。 |
**Orientation** | `Automatic` 會把橫幅照片轉成 1600×1200，並假設你是把相框**順時針**轉成橫放。如果照片上下顛倒，就選另一個 landscape 選項。 |
**Calibration（校準）** | 預設是這份韌體一直用的飽和調色盤，所以升級不會改變任何東西。建議先試 `Spectra 6 measured`，它是單一改動中效果最明顯的一項。 |
**Dithering kernel（核心）** | `Floyd-Steinberg` 是推薦值。`Jarvis` 與 `Stucki` 更平滑但更慢，`Atkinson` 對比更強（它刻意丟掉 2/8 的誤差）。 |
**Colour distance（色彩距離）** | `RGB` 最快也最好預期。`LAB` 比較接近人眼判斷差異的方式，每張影格大約多花一秒。 |
**Preset（預設）** | 照片從 `Balanced` 開始試。`Restore` 與 `Poster scan` 是給褪色掃描件與文件用的，也是唯二會先量測照片再決定參數的兩組。 |
**Clarity（清晰度）** | 最貴的一個階段：唯一需要看鄰近像素的，所以會多掃一遍來源。 |
**Brightness (gamma)** | 大於 1.0 變亮。它與 **Exposure** 功能重疊、而且出現得更早；除非你本來就在用，否則請留在 1.0。 |
**Cached frames** | 每張 960 KB。看過的照片再顯示時不用連網也不用解碼，對按鍵換圖體驗差很多。 |

秘密欄位（Wi-Fi 密碼、API key、管理密碼）**不會**被送回瀏覽器，欄位留空就是
「保留原本的值」。

---

## 測試

純計算的部分（校準調色盤、抖色器、抖色前處理、縮放/旋轉/打包）可以直接在電腦上
測，不需要裝置也不需要 PlatformIO：

```bash
./test/host/run.sh
```

六組測試：

- `test_palette` — 校準調色盤的各張表，逐個數字對照參考專案裡寫出來的 hex，另外
  測六格自訂欄位背後的 hex 解析。這裡打錯一個數字是看不出來的，而且會影響下游
  所有東西，所以值得釘死。
- `test_stream` — 證明串流式 3 列誤差緩衝與**整張圖**的參考實作
  **逐像素完全相同**，**十二種**核心、**兩種**掃描方向全部涵蓋。這是能抓到
  緩衝區輪替錯誤的那一個。
- `test_imgproc` — 各處理階段對照「獨立重寫一份的參考實作」：飽和度捷徑對照
  逐字照抄的 HSL 來回換算（證明兩者是同一個函式）、S 曲線對照
  `applyScurveTonemap()`、動態範圍壓縮必須精準落在調色盤的實測端點上、以及滑動
  視窗版清晰度對照整張圖的 box blur。
- `test_dither` — 各核心、各調色盤、各匹配模式的**顏色重建精度**，**外加**
  「預設設定仍然算繪出與這些功能存在之前完全相同的結果」這道回歸防線。注意：
  Spectra 6 沒有灰色，中灰的最近色其實是「綠色」，所以正確的檢驗標準是*區域平均
  還原出的 RGB*要接近輸入，而不是黑白點的比例。
- `test_render` — 四種旋轉的座標映射、cover 必須完全填滿、contain 的留白比例、
  4bpp 的 nibble 打包順序、預覽的降取樣，以及留白邊條必須是調色盤的白而不是
  `#FFFFFF`。
- `test_jpeg` — 證明 `lib/JPEGDEC` 的 patch 真的有效：內嵌一張 baseline JPEG，與
  「同一份 entropy data、header 改寫成 SOF1 + 第三張 AC Huffman 表」的版本，兩者
  必須解出逐像素完全相同的結果。沒有 patch 的話後者在 `open()` 就會失敗。

`test/host/Arduino.h` 是一個極簡的 Arduino API 替身，只為了讓那些檔案能在
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

## 瀏覽器燒錄頁（GitHub Pages）

`docs/` 是一頁式的 [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
安裝器，由 `.github/workflows/pages.yml` 在每次發佈 release 時自動部署。沒聽過
PlatformIO 的人只要用 Chrome 開那一頁、插上板子，就能燒。

網址是 **<https://pinetum.github.io/reterminal-immich-frame/>**。


### 為什麼不是由網頁直接去抓 release 附件

讓網頁在載入時直接抓 release 的檔案會更漂亮，但**做不到**：GitHub 的 release
附件由 `release-assets.githubusercontent.com` 提供，回應裡**完全沒有
`access-control-allow-origin`**，瀏覽器會擋下這個跨來源請求。改走
`api.github.com/repos/.../releases/assets/<id>` 也一樣 —— API 那層的 302 確實有
`access-control-allow-origin: *`，但它重導過去的附件主機沒有，而瀏覽器看的是
最終那個回應。

所以改成由 workflow 在伺服器端做（那裡沒有 CORS 的問題）：用 `gh release download`
把 `*-merged.bin` 抓下來，旁邊寫一份 `manifest.json`，連同網頁一起部署。這樣
binary 就與網頁同源，抓取不會有任何問題。燒錄來源確實就是 release 的附件，只是
在**部署時**解析，而不是在頁面載入時。

想重新發佈舊的 tag，或改完 `docs/` 之後想重建網頁，到
**Actions → Deploy web flasher → Run workflow** 手動跑並指定 tag 即可。

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
  render.cpp     串流式縮放 + 旋轉 + 處理 + 抖色 → packed 4bpp；預覽；測試色塊圖
  imgproc.cpp    抖色前各階段：色調、動態範圍、清晰度、暖紙中和、預設組
  e6_dither.cpp  串流式誤差擴散、有序／隨機抖色、三種色彩距離模型
  palette.cpp    校準調色盤的各張表與 hex 解析
  colorspace.cpp 查表式 sRGB ↔ linear ↔ CIELAB，給上面兩者共用
  webui.cpp      非阻塞的管理 API
  web_assets.h   管理頁（HTML/CSS/JS，放在 PROGMEM）
  tools/
    gpio_probe.cpp  獨立的硬體自檢（另一個 PlatformIO 環境，不會編進韌體）
include/         pins.h、settings.h、palette.h、e6_dither.h、imgproc.h、colorspace.h、log.h
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

**只要改到任何算繪設定，就會丟掉影格快取。** 快取裡的影格是「當時那組設定」畫出
來的樣子，沒有這個機制的話，換調色盤的效果會被完全遮住，看起來就像那個設定根本
沒作用。`settingsRenderSignature()` 會把所有影響算繪結果的欄位做雜湊，管理 API
在存檔前後比對它。

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
8. **有序與隨機抖色混不出中間色。** 沒有誤差回饋時，單一純量門檻只會沿著灰階軸
   移動，最近的那種墨水從頭到尾不變，所以一片平坦的中灰會輸出成單一顏色。
   epdoptimize 用 coverage-based Bayer 解決這件事，但那需要一次整張圖的 pass，
   我們負擔不起。照片請用誤差擴散。
9. **沒有藍雜訊抖色。** 它的遮罩是一個 3.8 MB 的 PNG，比整個韌體還大。
10. **校準流程是手動的。** 裝置負責畫出測試色塊圖；從照片上讀出六塊的顏色再填
    回去這一段要自己來。esp32-photoframe 把它自動化了（上傳照片、自動取樣格點），
    那需要一個上傳端點與格點偵測。

---

## 參考

- [reTerminal E1004 入門](https://wiki.seeedstudio.com/getting_started_with_reterminal_e1004/)
- [Arduino Cookbook: Onboard Peripherals（按鍵、LED、電池、SD 的腳位來源）](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino_peripherals_2/)
- [Seeed_GFX](https://github.com/Seeed-Studio/Seeed_GFX) — `EPaper` 類別與 `Setup523`
- [Immich API 文件](https://api.immich.app/endpoints)

影像管線是前人成果的移植，功勞屬於他們：

- [paperlesspaper/epdoptimize](https://github.com/paperlesspaper/epdoptimize) —— 校準調色盤、整套擴散核心、三種色彩距離模型、預處理堆疊與它的預設組。另有[互動工具](https://paperlesspaper.github.io/epdoptimize/)與[部落格文章](https://paperlesspaper.de/en/blog/dither-eink-tool-open-source)。
- [aitjcize/esp32-photoframe](https://github.com/aitjcize/esp32-photoframe) —— 同一套想法在 ESP32-S3 上用 C 實作，以及讓它在這種機器上跑得動的逐列串流與查表技巧。其中的 [MEASURED_PALETTE.md](https://github.com/aitjcize/esp32-photoframe/blob/main/docs/MEASURED_PALETTE.md) 是「為什麼實測調色盤重要」講得最清楚的一篇。
- [aitjcize/epaper-image-convert](https://github.com/aitjcize/epaper-image-convert) —— 這裡用的色調映射數學與參數慣例。
- epdoptimize 自己則感謝了 [DitherIt](https://ditherit.com/)、[dither-me-this](https://github.com/DitheringIdiot/dither-me-this)、[Inkify](https://github.com/cmdwtf/Inkify) 與 [eInk Dither Tester](https://github.com/mattcarter11/eink-dithering-tester)。
