# ZMK Custom Display Widget Repositories — Comparison

Research date: 2026-04-20

---

## Summary Table

| # | Repository | ★ | Display | Animation | Canvas | LVGL API | Module |
|---|-----------|-----|---------|-----------|--------|----------|--------|
| 1 | [nice-view-gem](https://github.com/M165437/nice-view-gem) | 370 | nice!view 160×68 1-bit | Crystal (16 frames, `lv_animimg`) | Yes (3 bufs) | v9 (`main`) / v8 (`v0.3`) | ZMK module |
| 2 | [zmk-dongle-screen (YADS)](https://github.com/janpfischer/zmk-dongle-screen) | 246 | ST7789V 280×240 RGB565 | None (static widgets) | Yes (battery) | v9 (`main`) / v8 (`v0.3`) | ZMK module |
| 3 | [zmk-dongle-display](https://github.com/englmaxi/zmk-dongle-display) | 244 | OLED SSD1306 128×64 1-bit | Bongo cat (WPM-reactive, 4 tiers) | Yes (battery) | v9 (`main`) / v8 (`v0.3`) | ZMK module |
| 4 | [prospector-zmk-module](https://github.com/carrefinho/prospector-zmk-module) | 151 | ST7789V 1.69″ IPS color | Layer roller animation | Yes | v8 (v0.3 only; Zephyr 4.1 WIP) | ZMK module |
| 5 | [nice-view-battery](https://github.com/infely/nice-view-battery) | 98 | nice!view 160×68 1-bit | None (static) | Yes | v9 / v8 | ZMK module |
| 6 | [zmk-dongle-display-view](https://github.com/mctechnology17/zmk-dongle-display-view) | 35 | nice!view 160×68 1-bit | Bongo cat (WPM-reactive, fork of englmaxi) | Yes (battery) | v8 (v0.3 only) | ZMK module |
| 7 | [nice-view-mod](https://github.com/GPeye/nice-view-mod) | 29 | nice!view 160×68 1-bit | None (stock nice!view clone) | Yes | v8 (v0.3 only) | ZMK module |

---

## 1. M165437/nice-view-gem — 370 ★, 146 forks

**URL:** https://github.com/M165437/nice-view-gem  
**Display:** nice!view Sharp Memory LCD (LS011B7DH03), 160×68, 1-bit  
**ZMK compat:** `main` (LVGL v9 / Zephyr 4.1) and `v0.3.0` release for older ZMK

### Widgets
- **Crystal animation** (peripheral side only): 16 frames of a floating crystal, 69×68 px, `LV_COLOR_FORMAT_I1`
- **WPM** gauge + chart with configurable fixed/dynamic range
- **Battery** with bolt charging indicator
- **Layer** name (central only)
- **Output/BLE** status with profile dots
- **Profile** indicator

### Animation Method
```c
lv_obj_t *art = lv_animimg_create(canvas);
lv_animimg_set_src(art, (const void **)anim_imgs, 16);
lv_animimg_set_duration(art, CONFIG_NICE_VIEW_GEM_ANIMATION_MS); // default 960
lv_animimg_set_repeat_count(art, LV_ANIM_REPEAT_INFINITE);
lv_animimg_start(art);
```
16 frames / 960 ms = ~60 fps. Configurable via `CONFIG_NICE_VIEW_GEM_ANIMATION_MS`.  
When animation disabled, shows random or fixed frame index.

### LV_USE_CANVAS
**Yes — extensively.** 3 canvas buffers (`cbuf`, `cbuf2`, `cbuf3`), each 68×68 pixels.
```c
#define CANVAS_SIZE 68
#define CANVAS_COLOR_FORMAT LV_COLOR_FORMAT_L8
#define CANVAS_BUF_SIZE LV_CANVAS_BUF_SIZE(68, 68, ...)
```
Uses `rotate_canvas()` → `lv_draw_sw_rotate(..., LV_DISPLAY_ROTATION_270, ...)` for the vertical nice!view orientation.

### Display Config (Kconfig.defconfig)
| Config | Value |
|--------|-------|
| `CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED` | **yes** (default) |
| `CONFIG_ZMK_DISPLAY_WORK_QUEUE_STACK_SIZE` | ZMK default (2048) |
| `CONFIG_LV_Z_VDB_SIZE` | **100** |
| `CONFIG_LV_Z_MEM_POOL_SIZE` | **8192** |
| `CONFIG_LV_DPI_DEF` | 161 |
| `CONFIG_LV_Z_BITS_PER_PIXEL` | 1 |
| `CONFIG_LV_COLOR_DEPTH` | 1 |
| `CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE` | ZMK default |
| `CONFIG_ZMK_DISPLAY_TICK_PERIOD_MS` | ZMK default |

### Custom Config Options
| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_NICE_VIEW_GEM_WPM_FIXED_RANGE` | bool | y | Fixed vs dynamic WPM range |
| `CONFIG_NICE_VIEW_GEM_WPM_FIXED_RANGE_MAX` | int | 100 | Max WPM for fixed range |
| `CONFIG_NICE_VIEW_GEM_ANIMATION` | bool | y | Enable/disable crystal animation |
| `CONFIG_NICE_VIEW_GEM_ANIMATION_FRAME` | int | 0 | Static frame index (1–16) when anim off |
| `CONFIG_NICE_VIEW_GEM_ANIMATION_MS` | int | 960 | Animation duration (all 16 frames) |
| `CONFIG_NICE_VIEW_WIDGET_INVERTED` | bool | n | Invert colors |

### Custom Overlays
None — uses nice_view_adapter shield.

---

## 2. janpfischer/zmk-dongle-screen (YADS) — 246 ★, 91 forks

**URL:** https://github.com/janpfischer/zmk-dongle-screen  
**Display:** ST7789V 1.69″ IPS LCD, 280×240, RGB565  
**ZMK compat:** `main` and `v0.3` branches

### Widgets
- **Output** status (USB/BLE with NerdFont icons)
- **Layer** name/index
- **Modifier** status (NerdFont icons at 20px/40px, with `k_timer` 100ms polling)
- **WPM** counter
- **Battery** (peripheral, with lv_canvas bar)

### Animation Method
No frame-based animations. Mod status uses `k_timer` polling at 100ms for reactive updates.

### LV_USE_CANVAS
**Yes** — battery widget uses `lv_canvas_create()`.

### Display Config (Kconfig.defconfig)
| Config | Value |
|--------|-------|
| `CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED` | **yes** (default) |
| `CONFIG_ZMK_DISPLAY_DEDICATED_THREAD_STACK_SIZE` | **4096** |
| `CONFIG_LV_Z_VDB_SIZE` | **100** |
| `CONFIG_LV_Z_MEM_POOL_SIZE` | **10000** |
| `CONFIG_LV_DPI_DEF` | 261 |
| `CONFIG_LV_Z_BITS_PER_PIXEL` | 16 |
| `CONFIG_LV_COLOR_DEPTH` | 16 |
| `CONFIG_LV_COLOR_16_SWAP` | y |
| `CONFIG_LV_DISP_DEF_REFR_PERIOD` | 20 |
| `CONFIG_PWM` | y |
| `CONFIG_LED` | y |

### Custom Config Options
| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_DONGLE_SCREEN_HORIZONTAL` | bool | y | Screen orientation |
| `CONFIG_DONGLE_SCREEN_FLIPPED` | bool | n | Flip orientation |
| `CONFIG_DONGLE_SCREEN_IDLE_TIMEOUT_S` | int | 600 | Screen off after idle (0=never) |
| `CONFIG_DONGLE_SCREEN_MAX_BRIGHTNESS` | int | 80 | Max PWM brightness (1-100) |
| `CONFIG_DONGLE_SCREEN_MIN_BRIGHTNESS` | int | 1 | Min brightness (1-99) |
| `CONFIG_DONGLE_SCREEN_DEFAULT_BRIGHTNESS` | int | =MAX | Initial brightness |
| `CONFIG_DONGLE_SCREEN_BRIGHTNESS_KEYBOARD_CONTROL` | bool | y | F22/F23/F24 control |
| `CONFIG_DONGLE_SCREEN_BRIGHTNESS_STEP` | int | 10 | Per-keystroke step |
| `CONFIG_DONGLE_SCREEN_WPM_ACTIVE` | bool | y | WPM widget toggle |
| `CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE` | bool | y | Modifier widget toggle |
| `CONFIG_DONGLE_SCREEN_LAYER_ACTIVE` | bool | y | Layer widget toggle |
| `CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE` | bool | y | Output widget toggle |
| `CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE` | bool | y | Battery widget toggle |
| `CONFIG_DONGLE_SCREEN_AMBIENT_LIGHT` | bool | n | APDS9960 auto-brightness |
| `CONFIG_DONGLE_SCREEN_SYSTEM_ICON` | int | 0 | GUI icon: 0=macOS, 1=Linux, 2=Windows |

### Custom Overlays
Yes — custom ST7789V driver (`drivers/display/display_st7789v.c`), custom LVGL module override (`modules/lvgl/lvgl.c`), PWM backlight control.

---

## 3. englmaxi/zmk-dongle-display — 244 ★, 93 forks

**URL:** https://github.com/englmaxi/zmk-dongle-display  
**Display:** OLED SSD1306 128×64 (or SH1106 1.3″), 1-bit. Also supports 128×32 with limitations.  
**ZMK compat:** `main` and `v0.3` branches

### Widgets
- **Bongo cat** — WPM-reactive animation with 4 speed tiers
- **WPM** meter with speedometer icon (throttled at 250ms)
- **Active modifiers** — Ctrl/Shift/Alt/GUI/Opt with animated selection indicator
- **Layer** name (scrolling when too wide)
- **Output** status (USB/BLE with animated selection line)
- **HID indicators** (CapsLock/NumLock/ScrollLock)
- **Peripheral battery** (multi-device)

### Animation Method
**WPM-reactive bongo cat with 4 speed tiers:**
```c
#define ANIMATION_SPEED_IDLE 10000   // blinking
#define ANIMATION_SPEED_SLOW 2000    // gentle tapping
#define ANIMATION_SPEED_MID  500     // moderate tapping
#define ANIMATION_SPEED_FAST 100     // furious tapping
```
Frame selection based on WPM thresholds. Animation updates throttled:
```c
#define ANIM_UPDATE_INTERVAL_MS 200  // max 5 animation checks/sec
```
Uses `lv_animimg_create()` + `lv_animimg_set_src()` + `lv_animimg_set_duration()`.

**Modifier slide animation:**
```c
lv_anim_set_duration(&a, 200);
lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
```

### LV_USE_CANVAS
**Yes** — battery widget: `lv_canvas_create()`, 5×8 px per icon, `LV_COLOR_FORMAT_L8`.
```c
#define BUFFER_SIZE LV_CANVAS_BUF_SIZE(5, 8, LV_COLOR_FORMAT_GET_BPP(LV_COLOR_FORMAT_L8), ...)
```

### Display Config (Kconfig.defconfig)
| Config | Value |
|--------|-------|
| `CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED` | **yes** (default) |
| `CONFIG_ZMK_DISPLAY_WORK_QUEUE_STACK_SIZE` | ZMK default (2048) |
| `CONFIG_LV_Z_VDB_SIZE` | **100** (README suggests 32 for 128×32 OLED) |
| `CONFIG_LV_Z_MEM_POOL_SIZE` | **16384** |
| `CONFIG_LV_DPI_DEF` | 148 |
| `CONFIG_LV_Z_BITS_PER_PIXEL` | 1 |
| `CONFIG_LV_COLOR_DEPTH` | 1 |
| `CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE` | ZMK default |
| `CONFIG_ZMK_DISPLAY_TICK_PERIOD_MS` | ZMK default |

### Custom Config Options
| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_ZMK_DONGLE_DISPLAY_BONGO_CAT` | bool | y | Bongo cat widget |
| `CONFIG_ZMK_DONGLE_DISPLAY_MODIFIERS` | bool | y | Modifiers widget |
| `CONFIG_ZMK_DONGLE_DISPLAY_LAYER` | bool | y | Layer widget |
| `CONFIG_ZMK_DONGLE_DISPLAY_WPM` | bool | n | WPM meter |
| `CONFIG_ZMK_DONGLE_DISPLAY_WPM_DISABLED_LAYERS` | string | "" | Comma-separated layers to disable WPM |
| `CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY` | bool | n | Show dongle battery |
| `CONFIG_ZMK_DONGLE_DISPLAY_MAC_MODIFIERS` | bool | n | macOS modifier icons |
| `CONFIG_ZMK_DONGLE_DISPLAY_LAYER_TEXT_ALIGN` | string | "left" | Layer text alignment |
| `CONFIG_ZMK_DONGLE_DISPLAY_LAYER_NAME_SCROLL_WIDTH` | int | 50 | Scroll threshold width (px) |

### Custom Overlays
None — uses standard SSD1306/SH1106 I2C display overlay in user config.

---

## 4. carrefinho/prospector-zmk-module — 151 ★, 142 forks

**URL:** https://github.com/carrefinho/prospector-zmk-module  
**Hardware:** https://github.com/carrefinho/prospector (732 ★)  
**Display:** ST7789V 1.69″ IPS LCD with curved glass, Waveshare module  
**ZMK compat:** `main` branch = v0.3 only; `feat/new-status-screens` branch = Zephyr 4.1 WIP

### Widgets
- **Layer roller** — animated roller showing highest active layer
- **Peripheral battery** bar
- **Peripheral connection** status
- **Caps word** indicator

### Animation Method
Layer roller uses LVGL roller widget with smooth scrolling animation (built-in LVGL).

### LV_USE_CANVAS
**Yes** — used for battery bar widget.

### Display Config
| Config | Value |
|--------|-------|
| `CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED` | likely yes (standard pattern) |
| `CONFIG_LV_Z_VDB_SIZE` | not specified (likely 100) |
| `CONFIG_LV_Z_MEM_POOL_SIZE` | not specified |

### Custom Config Options
| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR` | bool | y | APDS9960 auto-brightness |
| `CONFIG_PROSPECTOR_FIXED_BRIGHTNESS` | int | 50 | Fixed brightness (1-100) |
| `CONFIG_PROSPECTOR_ROTATE_DISPLAY_180` | bool | n | Rotate display |
| `CONFIG_PROSPECTOR_LAYER_ROLLER_ALL_CAPS` | bool | n | Uppercase layer names |

### Custom Overlays
Yes — custom ST7789V driver, ambient light sensor (APDS9960), custom LVGL module.

---

## 5. infely/nice-view-battery — 98 ★, 21 forks

**URL:** https://github.com/infely/nice-view-battery  
**Display:** nice!view Sharp Memory LCD, 160×68, 1-bit  
**ZMK compat:** `main` and `v0.3` branches

### Widgets
- **Battery** percentage with graphical indicator
- **Layer** name (hides empty layers)
- **Output/BLE** status

### Animation Method
None — static display, event-driven updates only.

### LV_USE_CANVAS
**Yes** — `LV_USE_CANVAS` selected in Kconfig.

### Display Config (Kconfig.defconfig)
| Config | Value |
|--------|-------|
| `CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED` | **yes** (default) |
| `CONFIG_LV_Z_VDB_SIZE` | **100** |
| `CONFIG_LV_Z_MEM_POOL_SIZE` | **4096** |
| `CONFIG_LV_DPI_DEF` | 161 |
| `CONFIG_LV_Z_BITS_PER_PIXEL` | 1 |
| `CONFIG_LV_COLOR_DEPTH` | 1 |

### Custom Config Options
| Option | Type | Default |
|--------|------|---------|
| `CONFIG_NICE_VIEW_WIDGET_INVERTED` | bool | n |

### Custom Overlays
None — uses nice_view_adapter.

---

## 6. mctechnology17/zmk-dongle-display-view — 35 ★, 12 forks

**URL:** https://github.com/mctechnology17/zmk-dongle-display-view  
**Display:** nice!view 160×68 1-bit (via `nice_view_adapter`)  
**ZMK compat:** v0.3 only (uses `LV_IMG_CF_INDEXED_1BIT` = LVGL v8)

### Widgets
Fork of englmaxi/zmk-dongle-display adapted for nice!view:
- **Bongo cat** — WPM-reactive (same 4-tier system as englmaxi)
- **Modifiers** with animated selection
- **Layer** name
- **Output** status with animated transitions
- **Battery** (peripheral + optional dongle)
- **HID indicators**

### Animation Method
Same as englmaxi — WPM-reactive bongo cat with `lv_animimg_create()`:
```c
#define ANIMATION_SPEED_IDLE 10000
#define ANIMATION_SPEED_SLOW 2000
#define ANIMATION_SPEED_MID  500
#define ANIMATION_SPEED_FAST 200   // note: 200ms vs englmaxi's 100ms
```

### LV_USE_CANVAS
**Yes** — battery widget: `lv_canvas_create()`, `LV_IMG_CF_TRUE_COLOR` (LVGL v8).

### Display Config (Kconfig.defconfig)
| Config | Value |
|--------|-------|
| `CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED` | **yes** (default) |
| `CONFIG_LV_Z_VDB_SIZE` | **64** |
| `CONFIG_LV_Z_MEM_POOL_SIZE` | **8192** |
| `CONFIG_LV_DPI_DEF` | 148 |
| `CONFIG_LV_Z_BITS_PER_PIXEL` | 1 |
| `CONFIG_LV_COLOR_DEPTH` | 1 |

### Custom Config Options
Inherits from englmaxi:
- `CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY`
- `CONFIG_ZMK_DONGLE_DISPLAY_MAC_MODIFIERS`

### Custom Overlays
None — uses `nice_view_adapter` shield. Supports `pro_micro` and `seeeduino_xiao_ble` pinouts.

---

## 7. GPeye/nice-view-mod — 29 ★, 126 forks

**URL:** https://github.com/GPeye/nice-view-mod  
**Display:** nice!view Sharp Memory LCD, 160×68, 1-bit  
**ZMK compat:** v0.3 only (LVGL v8)

### Widgets
Stock nice!view widget set (copy of official shield for easy forking):
- WPM gauge
- Battery
- Layer
- Output/BLE profile

### Animation Method
None in base — identical to official nice!view.  
Example fork with animations: [GPeye/urchin-peripheral-animation](https://github.com/GPeye/urchin-peripheral-animation).

### LV_USE_CANVAS
**Yes** — `LV_USE_CANVAS` selected in Kconfig.

### Display Config (Kconfig.defconfig)
| Config | Value |
|--------|-------|
| `CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED` | **yes** (default) |
| `CONFIG_LV_Z_VDB_SIZE` | **100** |
| `CONFIG_LV_Z_MEM_POOL_SIZE` | **4096** |
| `CONFIG_LV_DPI_DEF` | 161 |
| `CONFIG_LV_Z_BITS_PER_PIXEL` | 1 |
| `CONFIG_LV_COLOR_DEPTH` | 1 |

### Custom Config Options
| Option | Type | Default |
|--------|------|---------|
| `CONFIG_NICE_VIEW_WIDGET_INVERTED` | bool | n |

### Custom Overlays
None — uses nice_view_adapter. Shield name: `nice_view_custom`.

---

## Cross-Cutting Analysis

### Animation Techniques Used

| Technique | Repos Using It | How It Works |
|-----------|---------------|--------------|
| `lv_animimg` (frame sequence) | nice-view-gem, englmaxi, mctechnology17 | Set N images + duration → LVGL auto-cycles. Simple, low CPU. |
| WPM-reactive frame switching | englmaxi, mctechnology17 | Select animation tier based on WPM thresholds → swap `lv_animimg_set_src()` |
| `lv_anim_t` transitions | englmaxi, mctechnology17 | Property animations (x-position, size) with easing (`lv_anim_path_overshoot`) |
| `k_timer` polling | YADS | Zephyr kernel timer at fixed interval for mod status checks |
| LVGL roller widget | prospector | Built-in LVGL roller with smooth scroll for layer names |

### Memory Budget Comparison

| Repo | VDB Size | Mem Pool | Stack | Display | Notes |
|------|----------|----------|-------|---------|-------|
| nice-view-gem | 100% | 8 KB | default | 160×68×1bpp | 3× canvas bufs (68×68 L8 each ≈ 13.9 KB total) |
| YADS | 100% | 10 KB | 4096 | 280×240×16bpp | Color LCD needs much larger VDB |
| englmaxi | 100% | 16 KB | default | 128×64×1bpp | Largest pool — many widgets + animations |
| mctechnology17 | 64% | 8 KB | default | 160×68×1bpp | Reduced VDB vs englmaxi (nice!view is smaller) |
| nice-view-battery | 100% | 4 KB | default | 160×68×1bpp | Minimal — fewest widgets |
| nice-view-mod | 100% | 4 KB | default | 160×68×1bpp | Stock nice!view clone |

### LVGL Version Compatibility

| Repo | LVGL v9 (ZMK `main`) | LVGL v8 (ZMK `v0.3`) |
|------|----------------------|----------------------|
| nice-view-gem | ✅ `main` branch | ✅ `v0.3.0` release |
| YADS | ✅ `main` branch | ✅ `v0.3` branch |
| englmaxi | ✅ `main` branch | ✅ `v0.3` branch |
| prospector | ❌ (WIP `feat/new-status-screens`) | ✅ `main` branch |
| nice-view-battery | ✅ `main` branch | ✅ `v0.3` branch |
| mctechnology17 | ❌ (uses `LV_IMG_CF_INDEXED_1BIT`) | ✅ only |
| nice-view-mod | ❌ (uses `LV_IMG_CF_INDEXED_1BIT`) | ✅ only |

### Key Takeaways for Our Build

1. **All repos use `CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED=y`** — no exceptions. This is the standard for custom display widgets.

2. **VDB at 100% is standard** for 1-bit displays (nice!view, OLED). Only mctechnology17 uses 64%. YADS uses 100% for its color LCD.

3. **`LV_Z_MEM_POOL_SIZE` ranges from 4 KB to 16 KB** depending on widget complexity. 4 KB for minimal, 8 KB for moderate, 16 KB for feature-rich.

4. **Canvas is universal** — every single repo uses `LV_USE_CANVAS`. nice-view-gem uses it most extensively (3 buffers with rotation).

5. **`lv_animimg` is the standard animation method** — used by the top 3 animated repos. It's simple (set frames + duration) and efficient.

6. **WPM-reactive animation** (englmaxi pattern) is the most sophisticated: 4 speed tiers with throttled updates. Worth studying for our bongo cat.

7. **LVGL v9 migration is critical** — repos stuck on v8 (`LV_IMG_CF_INDEXED_1BIT` → `LV_COLOR_FORMAT_I1`) will break on ZMK `main`.
