// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The firmware's own effect engine, through cgo, for preview on this computer.
// Contributors: None

// Package engine draws an effect's frames with the firmware's C++ code, so that a preview here
// cannot differ from the garland: there is one engine, not two to keep alike. It needs a C++20
// compiler to build, as the firmware's host tests do.
package engine

/*
// The limit is platformio.ini's, which a test holds this to.
#cgo CPPFLAGS: -I${SRCDIR}/../../firmware/app/src -I${SRCDIR} -DCSIGARLAND_POWER_LIMIT_MA=1400
#cgo CXXFLAGS: -std=c++20 -DCSIGARLAND_MAX_LEDS=256
// The only C here is cJSON, a third party's, left as it came.
#cgo CFLAGS: -w
#cgo windows LDFLAGS: -static-libstdc++ -static-libgcc -static
#cgo !windows LDFLAGS: -lstdc++ -lm
#include <stdint.h>
#include "bridge.h"
*/
import "C"

import (
	"bytes"
	"errors"
	"sync"
	"unsafe"
)

// MaxLeds is the longest chain the engine draws, as in the firmware.
const MaxLeds = int(C.CSG_MAX_LEDS)

// LimitMilliamps is the current the firmware holds every frame to, by scaling it down.
const LimitMilliamps = int(C.CSG_POWER_LIMIT_MA)

const (
	bytesPerLed = 3
	errorBytes  = 200
)

// Color is one LED's colour.
type Color struct{ R, G, B uint8 }

// The firmware's engine has one set of buffers, as a garland plays one effect.
var engineLock sync.Mutex

// Check says what is wrong with an effect's file, or nil if the firmware would take it.
func Check(file []byte) error {
	engineLock.Lock()
	defer engineLock.Unlock()
	// Not load: the engine's effect belongs to whichever preview is active.
	return parse(file, func(text, message *C.char, size C.int) C.int { return C.csg_check(text, message, size) })
}

func load(file []byte) error {
	return parse(file, func(text, message *C.char, size C.int) C.int { return C.csg_load(text, message, size) })
}

func parse(file []byte, step func(text, message *C.char, size C.int) C.int) error {
	// C would stop reading there, and judge less than the garland would be sent.
	if bytes.IndexByte(file, 0) >= 0 {
		return errors.New("the file holds a NUL byte")
	}
	text := C.CString(string(file))
	defer C.free_text(text)
	message := make([]byte, errorBytes)
	if step(text, (*C.char)(unsafe.Pointer(&message[0])), C.int(len(message))) == 0 {
		end := 0
		for end < len(message) && message[end] != 0 {
			end++
		}
		return errors.New(string(message[:end]))
	}
	return nil
}

// Algorithms are the names the firmware's parser takes for a layer's fx, in its own order.
func Algorithms() []string {
	return names(int(C.csg_algorithm_count()), func(i C.int) *C.char { return C.csg_algorithm_name(i) })
}

// Figurines are the names a zone may list, in the order of the layout.
func Figurines() []string {
	return names(int(C.csg_figurine_count()), func(i C.int) *C.char { return C.csg_figurine_name(i) })
}

// Static strings of the firmware's, which nothing frees.
func names(count int, name func(C.int) *C.char) []string {
	out := make([]string, count)
	for i := range out {
		out[i] = C.GoString(name(C.int(i)))
	}
	return out
}

// Run draws a file from its start at each of the moments given, in order, on leds LEDs at full
// brightness, and returns the frames one after another. The same file draws the same, what is random
// in it too. It is apart from every preview: the active one starts again at its next frame.
func Run(file []byte, times []uint32, leds int) ([]Color, error) {
	count := min(leds, MaxLeds)
	if count <= 0 {
		return nil, errors.New("no LEDs to draw")
	}
	engineLock.Lock()
	defer engineLock.Unlock()
	active = nil
	if err := load(file); err != nil {
		return nil, err
	}
	C.csg_run_start()
	raw := make([]byte, count*bytesPerLed)
	out := make([]Color, 0, len(times)*count)
	for _, at := range times {
		C.csg_run_render(C.uint32_t(at), (*C.uint8_t)(unsafe.Pointer(&raw[0])), C.int(count))
		for i := range count {
			out = append(out, Color{raw[i*bytesPerLed], raw[i*bytesPerLed+1], raw[i*bytesPerLed+2]})
		}
	}
	return out, nil
}

// Preview plays one effect from its start.
type Preview struct {
	file      []byte
	valid     bool
	milliamps int
}

// NewPreview takes an effect's file, and says if the firmware would refuse it.
func NewPreview(file []byte) (*Preview, error) {
	engineLock.Lock()
	defer engineLock.Unlock()
	if err := load(file); err != nil {
		return nil, err
	}
	C.csg_reset()
	active = &Preview{file: append([]byte(nil), file...), valid: true}
	return active, nil
}

// Which preview the engine's buffers now belong to, as one may take over from another.
var active *Preview

// Frame draws the effect at a moment of its life into out, at a brightness in percent as the garland
// applies it, and returns how many LEDs it filled.
func (p *Preview) Frame(timeMs uint32, brightness int, out []Color) int {
	if !p.valid || len(out) == 0 {
		return 0
	}
	count := min(len(out), MaxLeds)
	engineLock.Lock()
	defer engineLock.Unlock()
	if active != p {
		if load(p.file) != nil {
			return 0
		}
		C.csg_reset()
		active = p
	}
	raw := make([]byte, count*bytesPerLed)
	p.milliamps = int(C.csg_render(C.uint32_t(timeMs), C.int(brightness), (*C.uint8_t)(unsafe.Pointer(&raw[0])), C.int(count)))
	for i := 0; i < count; i++ {
		out[i] = Color{raw[i*bytesPerLed], raw[i*bytesPerLed+1], raw[i*bytesPerLed+2]}
	}
	return count
}

// Milliamps is what the last frame drew, by the firmware's own model, before its limit held it back:
// more than LimitMilliamps is a frame the garland sends dimmer.
func (p *Preview) Milliamps() int { return p.milliamps }

// Currents is what an effect draws at each of times, by the firmware's own model, on a chain of leds
// at a brightness in percent, before the limit. A run of its own, as Run's.
func Currents(file []byte, times []uint32, brightness, leds int) ([]int, error) {
	count := min(leds, MaxLeds)
	if count <= 0 {
		return nil, errors.New("no LEDs to draw")
	}
	engineLock.Lock()
	defer engineLock.Unlock()
	active = nil
	if err := load(file); err != nil {
		return nil, err
	}
	C.csg_run_start()
	out := make([]int, len(times))
	for i, at := range times {
		out[i] = int(C.csg_run_current(C.uint32_t(at), C.int(brightness), C.int(count)))
	}
	return out, nil
}

// BrightnessShare is how much of its full current a frame draws at a brightness in percent: the
// firmware's gain, which every channel is scaled by. The current is linear in it.
func BrightnessShare(percent int) float64 {
	return float64(C.csg_brightness_gain(C.int(percent))) / 65536
}

// IdleMilliamps is what a chain of leds draws all dark, by the firmware's model: the part of the
// current no brightness scales.
func IdleMilliamps(leds int) int {
	engineLock.Lock()
	defer engineLock.Unlock()
	return int(C.csg_idle_current(C.int(min(max(leds, 0), MaxLeds))))
}
