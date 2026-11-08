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
// The runs apart from the preview, each with an effect, a renderer, buffers and power settings of its own:
// one for the samples of what a layer's fields change, one for Tune's chart of the current.
#define CSG_RUN_SAMPLES 0
#define CSG_RUN_CURRENTS 1
#define CSG_RUNS 2

#ifdef __cplusplus
extern "C" {
#endif

// Non-zero if the firmware's parser takes the file. Otherwise the reason is in error. cJSON keeps where a
// parse failed in a global of its own, so no two of these, csg_load and csg_run_load run at once.
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
// A run apart from the preview, which touches nothing of the preview's, nor of another run's: a renderer
// of its own made again from a fixed seed, and the file, as csg_load takes it, drawn on it. Non-zero if
// the parser takes the file. Each run is for one caller at a time.
int csg_run_load(int run, const char* json, char* error, int error_bytes);
// The run's frame at time_ms at full brightness.
void csg_run_render(int run, uint32_t time_ms, uint8_t* rgb, int count);
// The power settings the run's current is worked out by, as csg_set_power takes them: non-zero if taken.
int csg_run_power(int run, int supply_ma, int white_ma, int idle_ma, int margin_percent);
// The current a frame of the run draws at a brightness, as csg_render says it, drawing nothing.
uint32_t csg_run_current(int run, uint32_t time_ms, int brightness_percent, int count);
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
