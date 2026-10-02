# App 啟動器（esp32-hub 分支）

在原本的 Clawdmeter 上加了一層「小手錶系統」：

```
時鐘首頁 ──左右滑──▶ App 列表 ──點圖示──▶ App
    ▲                    │                  │
    └──── 從底部橫條往上滑（任何畫面）◀──────┘
```

- **首頁**：大時鐘＋日期，右上角電量。時間來自電腦端常駐程式（見下方「時鐘」）。
- **App 列表**：2×2 圖示格。左右滑回首頁。
- **App 內**：底部有一條橫條，從那裡往上滑回首頁；從螢幕最左邊往右滑回 App 列表。
- **中間 PWR 鍵**：交給目前的 App 處理；App 沒處理時就是調整亮度。

## 目前的 App

| App | 檔案 | 狀態 |
|---|---|---|
| Claude | `apps/app_claude.cpp` | 原本的用量畫面。PWR 鍵 → Clawd 動畫，再按換下一個動畫；點螢幕切回用量 |
| Weather | `apps/app_placeholders.cpp` | 佔位頁（Coming soon） |
| Smart Plug | `apps/app_placeholders.cpp` | 佔位頁 |
| Now Playing | `apps/app_placeholders.cpp` | 佔位頁 |

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
