/*
 * Bongo cat animation — native LVGL I1 image descriptors.
 */

#pragma once

#include <lvgl.h>

#define BONGO_W 54
#define BONGO_H 32

#define BONGO_IDLE_COUNT 5
#define BONGO_PREP_COUNT 1
#define BONGO_TAP_COUNT 2

extern const lv_image_dsc_t *const bongo_idle[];
extern const lv_image_dsc_t *const bongo_prep[];
extern const lv_image_dsc_t *const bongo_tap[];
