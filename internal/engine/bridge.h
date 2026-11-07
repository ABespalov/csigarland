/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: What Go calls of the firmware's effect engine
 * Contributors: None
 */

#ifndef CSIGARLAND_ENGINE_BRIDGE_H
#define CSIGARLAND_ENGINE_BRIDGE_H

#include <stdint.h>
#include <stdlib.h>

#define CSG_MAX_LEDS 256

#ifdef __cplusplus
extern "C" {
#endif

// Non-zero if the firmware's parser takes the file. Otherwise the reason is in error.
int csg_check(const char* json, char* error, int error_bytes);
// As csg_check, and the file is then what csg_render draws.
int csg_load(const char* json, char* error, int error_bytes);
// The names the parser takes for a layer's fx, and for the figurines of a zone, in its order.
int csg_algorithm_count(void);
const char* csg_algorithm_name(int index);
int csg_figurine_count(void);
const char* csg_figurine_name(int index);
void csg_reset(void);
// The frame at time_ms, at a brightness of 1 to 100 percent, as the garland would send it. Returns the
// current the firmware's model says it draws, in mA, before the limit holds it back.
uint32_t csg_render(uint32_t time_ms, int brightness_percent, uint8_t* rgb, int count);
// A run apart from the preview: a renderer of its own from a fixed seed, and the loaded file drawn
// on it at full brightness.
void csg_run_start(void);
void csg_run_render(uint32_t time_ms, uint8_t* rgb, int count);
// The current a frame of the run draws at a brightness, as csg_render says it, drawing nothing.
uint32_t csg_run_current(uint32_t time_ms, int brightness_percent, int count);
// What a chain of count LEDs draws all dark, by the firmware's model: what no brightness scales.
uint32_t csg_idle_current(int count);
// The firmware's gain for a brightness in percent, of 65536 at full.
uint32_t csg_brightness_gain(int brightness_percent);
// The firmware's power settings, as a garland starts with them: supply, white, idle and margin.
void csg_default_power(int* supply_ma, int* white_ma, int* idle_ma, int* margin_percent);
// What every current here is worked out by from now on, and the limit: non-zero if the firmware takes
// them, else the ones before stay.
int csg_set_power(int supply_ma, int white_ma, int idle_ma, int margin_percent);
// The limit the firmware works out from the settings in use, in mA.
uint32_t csg_power_limit(void);

#ifdef __cplusplus
}
#endif

static inline void free_text(char* text) {
    free(text);
}

#endif
