# App 啟動器（esp32-hub 分支）

在原本的 Clawdmeter 上加了一層「小手錶系統」：

```
時鐘首頁 ──左右滑──▶ App 列表 ──點圖示──▶ App
    ▲                    │                  │
    └──── 從底部橫條往上滑（任何畫面）◀──────┘
```

- **首頁**：大時鐘＋日期，右上角電量。時間來自電腦端常駐程式（見下方「時鐘」）。
- **App 列表**：三欄圖示，可上下捲動。左右滑回首頁。
- **App 內**：底部有一條橫條，從那裡往上滑回首頁；從螢幕最左邊往右滑回 App 列表。
- **中間 PWR 鍵**：交給目前的 App 處理；App 沒處理時就是調整亮度。

## 目前的 App

| App | 檔案 | 說明 |
|---|---|---|
| Claude | `apps/app_claude.cpp` | 原本的用量畫面。PWR 鍵 → Clawd 動畫，再按換下一個；點螢幕切回用量 |
| 正在播放 | `apps/app_now_playing.cpp` | 電腦正在播的歌，上一首／播放暫停／下一首；PWR = 播放暫停 |
| 天氣 | `apps/app_placeholders.cpp` | 佔位頁 |
| 智慧插座 | `apps/app_smart_plug.cpp` | TP-Link Tapo 插座（P100/P105/P110）開關，最多 3 個；透過電腦端常駐程式控制 |
| 頻譜 | `apps/app_spectrum.cpp` | 麥克風（ES7210）即時頻譜；PWR = 暫停（官方範例 05_Spec_Analyzer） |
| 設定 | `apps/app_settings.cpp` | 儲存空間（韌體、PSRAM、記憶體、Flash、設定儲存）、電池（電量、電壓、溫度）、藍牙、亮度、測試音、關於（晶片、溫度、開機時間、版本）（官方範例 03_LVGL_AXP2101_ADC_Data） |

不要的 App：刪掉它的 `.cpp`，再從 `app_registry.cpp` 刪掉那兩行即可。

### 板子額外功能（`hal/hal_extras.h`）

電源細節、加速度計、麥克風是「選配」介面，`hal_extras_default.cpp` 裡有預設的「不支援」版本，板子資料夾有實作的才會用到硬體：

- 2.16 S3：電源、加速度計、麥克風（ES7210，驅動來自 Waveshare 範例 06_ES7210）
- 2.16 C6：電源、加速度計
- 其他板子：App 會顯示「沒有…」
- 加速度計介面（`imu_hal_accel` / `imu_hal_app_mode`）先保留，之後要做體感相關 App 可以直接用

LVGL 改用自訂的記憶體配置（`-DLV_USE_STDLIB_MALLOC=255`，見 `lv_mem_psram.c`）而不是固定 64 KB 記憶體池：有 PSRAM 的板子把畫面物件放到 PSRAM，內部記憶體留給藍牙和系統。

## Now Playing（正在播放）

- **控制**：板子用藍牙鍵盤的「多媒體鍵」（播放暫停、上一首、下一首），不需要電腦端程式也能用。
- **歌名顯示**：電腦端常駐程式讀取 Windows 的媒體控制（按音量鍵時左上角跳出的那個），瀏覽器裡的 YouTube、YouTube Music 都會回報到那裡，每秒檢查一次，有變化才傳給板子，另外每 15 秒送一次心跳。
- 需要的 Python 套件已加進 `daemon/requirements-windows.txt`（`winrt-Windows.Media.Control` 等），重跑一次 `install-windows.ps1` 即可安裝。
- **第一次燒這版要重新配對藍牙**：藍牙鍵盤的描述多了多媒體鍵，Windows 會記住舊的描述。到「設定 → 藍牙與裝置」移除 Clawdmeter，再重新新增。
- 中文字型 `font_cjk_28.c`（Noto Sans CJK TC Medium，SIL OFL）：Big5 常用字 + JIS 第一水準漢字 + GB2312 一級字 + 假名，約 7800 字。字型超過 1 MB，所以每個 env 都加了 `-DLV_FONT_FMT_TXT_LARGE=1`。
- 2.16 S3 板改用 16 MB 分割表（`default_16MB.csv`）才放得下字型；NVS 位置不變，配對資料會保留。

模擬器：`SIM_NOWPLAYING='{"np":1,"ti":"歌名","ar":"歌手","app":"Chrome","st":"playing","pos":83,"dur":261}'`

## 智慧插座（TP-Link Tapo）

板子沒有 Wi-Fi，所以由電腦端常駐程式透過家裡的網路控制插座（Python `tapo` 套件），再用藍牙（特徵值 `…0006`）跟板子交換狀態和開關指令。電腦要開著、常駐程式要在跑，而且要跟插座在同一個網路。

1. **Tapo App**：「我」→「第三方服務」→「第三方相容性」打開（新版韌體要開這個才允許區域網路控制）。
2. **固定插座 IP**：Tapo App 裡點插座 → 右上齒輪 →「裝置資訊」可以看到 IP；建議在路由器把這三個 IP 設成固定（DHCP 保留），不然重開機可能會換。
3. **設定檔** `%LOCALAPPDATA%\Clawdmeter\config` 加上：

   ```
   tapo_username = 你的 Tapo 帳號 Email
   tapo_password = 你的 Tapo 密碼
   plugs = 192.168.1.50, 192.168.1.51, 192.168.1.52
   # 可選，不填就用 Tapo App 裡取的名字
   plug_names = 檯燈, 電風扇, 除濕機
   ```

   密碼是明碼存在這個檔案裡（只有你的 Windows 帳號讀得到），密碼裡有 `#` 也沒關係。
4. 重跑 `install-windows.ps1`（會裝 `tapo` 套件），常駐程式會自動重啟。設定檔改了不用重啟，下一次輪詢（約 10 秒）就會套用。

板子上點一下卡片就開／關，畫面會先切換，常駐程式確認後再更新；8 秒沒確認就退回原狀態。插座連不到會顯示「離線」。

模擬器：`SIM_PLUGS='{"pl":[{"n":"檯燈","on":1,"ok":1},{"n":"電風扇","on":0,"ok":1},{"n":"除濕機","on":0,"ok":0}]}'`

## 新增 App

1. 複製 `firmware/src/apps/app_template.cpp.txt` 成 `app_<名稱>.cpp`
2. 在 `app_registry.cpp` 宣告並加進 `APPS[]`

每個 App 是獨立檔案，新增 App 不會動到其他 App。佔位 App 真正實作時，從 `app_placeholders.cpp` 搬到自己的檔案即可。

## 時鐘

首頁時間來自電腦端常駐程式送來的 `t` 欄位，預設是**關閉**的。在 Windows 打開
`%LOCALAPPDATA%\Clawdmeter\config`（沒有就新建），加一行：

```
clock = 24
```

（`12` 為 12 小時制，`auto` 跟隨 Windows 設定）然後重新啟動常駐程式。之後做天氣 App 時會加上 Wi-Fi，到時可改用網路校時，不需要電腦。

## 字型與圖示

- `font_clock_120.c`：首頁大時鐘（Styrene B，只含數字和冒號）
- `font_icons_64.c`：App 圖示（Font Awesome 6 Free Solid，CC BY 4.0）

介面字型只有英文字元，所以畫面文字目前是英文。新增圖示：

```bash
npx lv_font_conv --font fa-solid-900.ttf -r 0xf6c4,0xf1e6,0xf001,0xf0c2,0xf185,<新的碼位> \
  --size 64 --format lvgl --bpp 4 --no-compress -o firmware/src/font_icons_64.c --lv-include "lvgl.h"
```

產生後照 `docs/fonts.md` 的說明修補成 LVGL 9 格式，並在 `apps/app.h` 加上對應的 `ICON_*` 定義。

## 模擬器測試

`SIM_INPUT=<檔案>` 可以用腳本模擬觸控和按鍵，無螢幕環境也能截圖：

```
1500 shot home.bmp
2000 down 400 240
2080 move 300 240
2160 up
2700 shot launcher.bmp
4200 pwr
8800 quit
```
