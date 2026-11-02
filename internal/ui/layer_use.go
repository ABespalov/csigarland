// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Which of a layer's fields its algorithm reads, found by the firmware's engine drawing it.
// Contributors: None

package ui

import (
	"slices"
	"sync"

	"github.com/ABespalov/csigarland/internal/engine"
)

// layerUse is what changes the light of an algorithm. A control that would change nothing is not
// offered: rain, say, draws its drops from its palette only, so its colours count only while it has
// none of its own.
type layerUse struct {
	sx, ix bool
	// How many colours of col count, as the last one that changes anything: with no palette of the
	// layer's own, when the palette is made of the colours, and with one. Sparkle, say, takes its
	// sparks from the first and its ground from the second, and never a third.
	col, colPal int
	pal         bool
}

// Colours is how many of col count for a layer with a palette of its own or without one.
func (u layerUse) Colours(ownPalette bool) int {
	if ownPalette {
		return u.colPal
	}
	return u.col
}

const (
	useFrames = 180
	useStepMs = 113 // Not a round number, so as not to fall in step with an algorithm's own period.
)

var (
	usesLock sync.Mutex
	uses     = map[string]layerUse{}
)

// algorithmUse draws the algorithm with one field changed at a time, and compares what it drew over
// some 20 s. The engine knows best what it reads: a table kept by hand would drift from it. It is
// found once for each algorithm, as the engine draws the same from the same file.
func algorithmUse(fx string) layerUse {
	usesLock.Lock()
	defer usesLock.Unlock()
	if use, known := uses[fx]; known {
		return use
	}
	times := make([]uint32, useFrames)
	for i := range times {
		times[i] = uint32(i * useStepMs)
	}
	colours := `"col":["FF4000","00C0FF","80FF00"]`
	// The same colours with one of them another.
	others := func(slot int) string {
		col := []string{"FF4000", "00C0FF", "80FF00"}
		col[slot] = "2040FF"
		return `"col":` + jsonText(col)
	}
	const (
		gradient = `"pal":[0,"FF0000",255,"0000FF"]`
		another  = `"pal":[0,"00FF00",128,"FFFFFF",255,"FF00FF"]`
	)
	draw := func(fields string) []engine.Color {
		file := `{"format":"csigarland.effect/1","name":"Use","layers":[{"fx":` + jsonText(fx) + `,` + fields + `}]}`
		frames, err := engine.Run([]byte(file), times, previewLeds)
		if err != nil {
			return nil
		}
		return frames
	}
	base := draw(`"sx":128,"ix":128,` + colours)
	withPal := draw(`"sx":128,"ix":128,` + colours + `,` + gradient)
	if base == nil || withPal == nil {
		// A name the parser does not take: the file's error says so, and its controls stay to mend it.
		return layerUse{sx: true, ix: true, col: maxLayerColors, colPal: maxLayerColors, pal: true}
	}
	differs := func(fields string, from []engine.Color) bool { return !slices.Equal(draw(fields), from) }
	use := layerUse{
		sx:  differs(`"sx":40,"ix":128,`+colours, base),
		ix:  differs(`"sx":128,"ix":220,`+colours, base),
		pal: differs(`"sx":128,"ix":128,`+colours+`,`+another, withPal),
	}
	for slot := range maxLayerColors {
		if differs(`"sx":128,"ix":128,`+others(slot), base) {
			use.col = slot + 1
		}
		if differs(`"sx":128,"ix":128,`+others(slot)+`,`+gradient, withPal) {
			use.colPal = slot + 1
		}
	}
	uses[fx] = use
	return use
}
