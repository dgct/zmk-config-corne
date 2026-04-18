/*
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Modified: Added peripheral battery level display for split keyboards.
 *
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include "status.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/wpm.h>
#include <zmk/events/activity_state_changed.h>
#include "bongo_cat_images.h"

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

// --- Bongo cat animation state ---
//
// Three-state machine driven by WPM:
//   IDLE (wpm <= 20)         : 5-frame slow tail wag, 800ms period
//   PREP (20 < wpm < 40)     : 1 static frame, 400ms period (no redraw needed)
//   TAP  (wpm >= 40)         : 2-frame paw tap, 100-500ms period (WPM-scaled)
//
// All bongo state lives here. draw_top renders whatever current_bongo_img
// points at; bongo_tick_cb is the sole writer of current_bongo_img and
// only triggers a redraw when the frame pointer actually changes.
#define BONGO_IDLE_SPEED 20
#define BONGO_TAP_SPEED 40
#define BONGO_ANIM_MS_IDLE 800
#define BONGO_ANIM_MS_PREP 400
#define BONGO_ANIM_MS_TAP_MAX 500
#define BONGO_ANIM_MS_TAP_MIN 100
#define BONGO_ANIM_WPM_CAP 120

enum bongo_state {
    BONGO_STATE_IDLE,
    BONGO_STATE_PREP,
    BONGO_STATE_TAP,
};

static enum bongo_state current_bongo_state = BONGO_STATE_IDLE;
static uint8_t bongo_frame = 0;
static const lv_image_dsc_t *current_bongo_img = NULL;
static lv_timer_t *bongo_timer = NULL;
static void bongo_tick_cb(lv_timer_t *timer);

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool profiles_connected[NICEVIEW_PROFILE_COUNT];
    bool profiles_bonded[NICEVIEW_PROFILE_COUNT];
};

struct layer_status_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

struct wpm_status_state {
    uint8_t wpm;
};

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
struct peripheral_battery_status_state {
    uint8_t level;
};
#endif

static void draw_top(lv_obj_t *widget, const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);

    // Fill background
    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    char output_text[10] = {};

    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        strcat(output_text, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (state->active_profile_bonded) {
            if (state->active_profile_connected) {
                strcat(output_text, LV_SYMBOL_WIFI);
            } else {
                strcat(output_text, LV_SYMBOL_CLOSE);
            }
        } else {
            strcat(output_text, LV_SYMBOL_SETTINGS);
        }
        break;
    }

    canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, output_text);

    // Draw current bongo frame (set by bongo_tick_cb).
    // current_bongo_img is initialized in zmk_widget_status_init before
    // any draw_top can run, so this is always non-NULL.
    if (current_bongo_img != NULL) {
        lv_draw_image_dsc_t img_dsc;
        lv_draw_image_dsc_init(&img_dsc);
        canvas_draw_img(canvas, 7, 36, current_bongo_img, &img_dsc);
    }

    // Rotate canvas
    rotate_canvas(canvas);
}

static void draw_middle(lv_obj_t *widget, const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);
    lv_draw_arc_dsc_t arc_dsc;
    init_arc_dsc(&arc_dsc, LVGL_FOREGROUND, 2);
    lv_draw_arc_dsc_t arc_dsc_filled;
    init_arc_dsc(&arc_dsc_filled, LVGL_FOREGROUND, 9);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_dsc_black;
    init_label_dsc(&label_dsc_black, LVGL_BACKGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);

    // Draw circles
    int circle_offsets[NICEVIEW_PROFILE_COUNT][2] = {
        {13, 13}, {55, 13}, {34, 34}, {13, 55}, {55, 55},
    };

    for (int i = 0; i < NICEVIEW_PROFILE_COUNT; i++) {
        bool selected = i == state->active_profile_index;

        if (state->profiles_connected[i]) {
            canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 13, 0, 360,
                            &arc_dsc);
        } else if (state->profiles_bonded[i]) {
            const int segments = 8;
            const int gap = 20;
            for (int j = 0; j < segments; ++j)
                canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 13,
                                360. / segments * j + gap / 2.0,
                                360. / segments * (j + 1) - gap / 2.0, &arc_dsc);
        }

        if (selected) {
            canvas_draw_arc(canvas, circle_offsets[i][0], circle_offsets[i][1], 9, 0, 359,
                            &arc_dsc_filled);
        }

        char label[2];
        snprintf(label, sizeof(label), "%d", i + 1);
        canvas_draw_text(canvas, circle_offsets[i][0] - 8, circle_offsets[i][1] - 10, 16,
                         (selected ? &label_dsc_black : &label_dsc), label);
    }

    // Rotate canvas
    rotate_canvas(canvas);
}

static void draw_bottom(lv_obj_t *widget, const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 2);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);

    // Draw layer name
    if (state->layer_label == NULL || strlen(state->layer_label) == 0) {
        char text[10] = {};
        sprintf(text, "LAYER %i", state->layer_index);
        canvas_draw_text(canvas, 0, 5, 68, &label_dsc, text);
    } else {
        canvas_draw_text(canvas, 0, 5, 68, &label_dsc, state->layer_label);
    }

    // Rotate canvas
    rotate_canvas(canvas);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

// --- Peripheral battery ---

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)

static void set_peripheral_battery_status(struct zmk_widget_status *widget,
                                          struct peripheral_battery_status_state state) {
    widget->state.peripheral_battery = state.level;
    draw_top(widget->obj, &widget->state);
}

static void peripheral_battery_status_update_cb(struct peripheral_battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_peripheral_battery_status(widget, state);
    }
}

static struct peripheral_battery_status_state
peripheral_battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);

    if (ev != NULL) {
        return (struct peripheral_battery_status_state){.level = ev->state_of_charge};
    }

    uint8_t level = 0;
    zmk_split_central_get_peripheral_battery_level(0, &level);
    return (struct peripheral_battery_status_state){.level = level};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_battery_status,
                            struct peripheral_battery_status_state,
                            peripheral_battery_status_update_cb,
                            peripheral_battery_status_get_state)

ZMK_SUBSCRIPTION(widget_peripheral_battery_status, zmk_peripheral_battery_state_changed);

#endif /* CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING */

// --- Output status ---

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;
    for (int i = 0; i < NICEVIEW_PROFILE_COUNT; ++i) {
        widget->state.profiles_connected[i] = state->profiles_connected[i];
        widget->state.profiles_bonded[i] = state->profiles_bonded[i];
    }

    draw_top(widget->obj, &widget->state);
    draw_middle(widget->obj, &widget->state);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    struct output_status_state state = {
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
    for (int i = 0; i < MIN(NICEVIEW_PROFILE_COUNT, ZMK_BLE_PROFILE_COUNT); ++i) {
        state.profiles_connected[i] = zmk_ble_profile_is_connected(i);
        state.profiles_bonded[i] = !zmk_ble_profile_is_open(i);
    }
    return state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

// --- Layer status ---

static void set_layer_status(struct zmk_widget_status *widget, struct layer_status_state state) {
    widget->state.layer_index = state.index;
    widget->state.layer_label = state.label;

    draw_bottom(widget->obj, &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_status(widget, state); }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

// --- Bongo cat animation timer ---
//
// Uses an LVGL timer (not a Zephyr work item) so frame advancement runs
// inside `lv_task_handler()` — the exact same thread as every
// ZMK_DISPLAY_WIDGET_LISTENER callback. This makes concurrent canvas
// writes impossible by construction.
//
// Lifecycle:
//   - Created in zmk_widget_status_init, runs continuously while the
//     display is active.
//   - When CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE=y, ZMK stops calling
//     lv_task_handler() on idle, freezing this timer automatically.
//   - We additionally pause/resume on activity state changes so the
//     timer is always quiescent when not visible.

static enum bongo_state bongo_state_for_wpm(uint8_t wpm) {
    if (wpm >= BONGO_TAP_SPEED) {
        return BONGO_STATE_TAP;
    }
    if (wpm > BONGO_IDLE_SPEED) {
        return BONGO_STATE_PREP;
    }
    return BONGO_STATE_IDLE;
}

static uint32_t bongo_period_ms(enum bongo_state s, uint8_t wpm) {
    switch (s) {
    case BONGO_STATE_IDLE:
        return BONGO_ANIM_MS_IDLE;
    case BONGO_STATE_PREP:
        return BONGO_ANIM_MS_PREP;
    case BONGO_STATE_TAP: {
        // Linear interpolation across [TAP_SPEED, WPM_CAP] -> [TAP_MAX, TAP_MIN].
        if (wpm > BONGO_ANIM_WPM_CAP) {
            wpm = BONGO_ANIM_WPM_CAP;
        }
        uint32_t span = BONGO_ANIM_WPM_CAP - BONGO_TAP_SPEED;
        uint32_t pos = wpm - BONGO_TAP_SPEED;
        return BONGO_ANIM_MS_TAP_MAX -
               pos * (BONGO_ANIM_MS_TAP_MAX - BONGO_ANIM_MS_TAP_MIN) / span;
    }
    }
    return BONGO_ANIM_MS_IDLE;
}

static const lv_image_dsc_t *bongo_frame_for(enum bongo_state s, uint8_t frame) {
    switch (s) {
    case BONGO_STATE_IDLE:
        return bongo_idle[frame % BONGO_IDLE_COUNT];
    case BONGO_STATE_PREP:
        return bongo_prep[frame % BONGO_PREP_COUNT];
    case BONGO_STATE_TAP:
        return bongo_tap[frame % BONGO_TAP_COUNT];
    }
    return bongo_idle[0];
}

static void bongo_tick_cb(lv_timer_t *timer) {
    uint8_t wpm = zmk_wpm_get_state();
    enum bongo_state next_state = bongo_state_for_wpm(wpm);

    // On state transition: reset frame counter and timer period.
    if (next_state != current_bongo_state) {
        current_bongo_state = next_state;
        bongo_frame = 0;
        lv_timer_set_period(timer, bongo_period_ms(next_state, wpm));
    } else if (next_state == BONGO_STATE_TAP) {
        // Within TAP, retune period as WPM drifts.
        lv_timer_set_period(timer, bongo_period_ms(next_state, wpm));
    }

    const lv_image_dsc_t *next_img = bongo_frame_for(current_bongo_state, bongo_frame);
    bongo_frame++;

    // Frame-change gating: skip redraw if the image pointer is unchanged.
    // PREP has only one frame, so this skips every PREP tick after the first.
    if (next_img == current_bongo_img) {
        return;
    }
    current_bongo_img = next_img;

    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        draw_top(widget->obj, &widget->state);
    }
}

// --- WPM status ---
//
// We only store WPM in the widget state for completeness. The bongo timer
// reads zmk_wpm_get_state() directly each tick, so there is no need to
// trigger any redraw from this listener.

static void set_wpm_status(struct zmk_widget_status *widget, struct wpm_status_state state) {
    widget->state.wpm = state.wpm;
}

static void wpm_status_update_cb(struct wpm_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_wpm_status(widget, state); }
}

static struct wpm_status_state wpm_status_get_state(const zmk_event_t *eh) {
    return (struct wpm_status_state){.wpm = zmk_wpm_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm_status, struct wpm_status_state, wpm_status_update_cb,
                            wpm_status_get_state)
ZMK_SUBSCRIPTION(widget_wpm_status, zmk_wpm_state_changed);

// --- Activity state -> bongo timer pause/resume ---
//
// When the keyboard goes idle, pause the bongo timer so it doesn't fire
// even once on display resume. On wake, reset the timer (so it doesn't
// fire immediately as overdue) and resume it.
//
// Runs on the system event manager thread. lv_timer_pause/resume only
// flip a flag and do not touch the canvas, so this is safe to call
// outside the display work queue context.

static int bongo_activity_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL || bongo_timer == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_ACTIVE:
        lv_timer_reset(bongo_timer);
        lv_timer_resume(bongo_timer);
        break;
    case ZMK_ACTIVITY_IDLE:
    case ZMK_ACTIVITY_SLEEP:
        lv_timer_pause(bongo_timer);
        break;
    default:
        break;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(bongo_activity, bongo_activity_listener);
ZMK_SUBSCRIPTION(bongo_activity, zmk_activity_state_changed);

// --- Init ---

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_t *middle = lv_canvas_create(widget->obj);
    lv_obj_align(middle, LV_ALIGN_TOP_LEFT, 24, 0);
    lv_canvas_set_buffer(middle, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_obj_t *bottom = lv_canvas_create(widget->obj);
    lv_obj_align(bottom, LV_ALIGN_TOP_LEFT, -44, 0);
    lv_canvas_set_buffer(bottom, widget->cbuf3, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    widget_peripheral_battery_status_init();
#endif
    widget_output_status_init();
    widget_layer_status_init();
    widget_wpm_status_init();

    // Initialize bongo state machine before any draw_top can run.
    // bongo_tick_cb will replace this on its first invocation.
    if (current_bongo_img == NULL) {
        current_bongo_img = bongo_idle[0];
    }

    // Create the bongo animation timer on the LVGL task scheduler.
    // The timer fires inside lv_task_handler(), so it runs on the same
    // thread as every ZMK_DISPLAY_WIDGET_LISTENER callback.
    if (bongo_timer == NULL) {
        bongo_timer = lv_timer_create(bongo_tick_cb, BONGO_ANIM_MS_IDLE, NULL);
    }

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
