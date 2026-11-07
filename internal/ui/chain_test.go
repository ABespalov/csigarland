// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The preview's chain: where each cell stands, and how an LED's light is drawn.
// Contributors: None

package ui

import (
	"image/color"
	"strings"
	"testing"
	"time"

	"gioui.org/io/input"
	"gioui.org/op"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/engine"
)

func TestTheChainGoesBackAndForthFromTheBoard(t *testing.T) {
	const columns = 4
	want := [][2]int{{0, 0}, {0, 1}, {0, 2}, {0, 3}, {1, 3}, {1, 2}, {1, 1}, {1, 0}, {2, 0}, {2, 1}}
	for cell, place := range want {
		if line, column := chainPlace(cell, columns); line != place[0] || column != place[1] {
			t.Errorf("cell %d at line %d, column %d; want %v", cell, line, column, place)
		}
	}
}

func TestAWiderWindowHoldsMoreOfTheChainToALine(t *testing.T) {
	const cell, gap = 40, 14
	if got := chainColumns(cell, cell, gap); got != 2 {
		t.Fatalf("a narrow line holds %d", got)
	}
	if narrow, wide := chainColumns(600, cell, gap), chainColumns(1200, cell, gap); wide <= narrow {
		t.Fatalf("%d at 600, %d at 1200", narrow, wide)
	}
	if got := chainColumns(4*cell+3*gap, cell, gap); got != 4 {
		t.Fatalf("four cells just fit, and %d were put on the line", got)
	}
}

// The preview and Tune draw as many LEDs as the garland connected to sends to, 32 with none; a long chain is
// drawn smaller, to stay within a few lines, and numbered every few LEDs where the numbers would run together.
func TestTheChainIsAsLongAsTheGarlands(t *testing.T) {
	a := sceneTune(t)
	a.client = nil
	if a.chainLeds() != previewLeds {
		t.Fatalf("with no garland, %d", a.chainLeds())
	}
	a.client = &api.Client{}
	a.state.Leds = api.Leds{Count: 100, Setting: 100}
	if a.chainLeds() != 100 {
		t.Fatalf("set to 100, %d", a.chainLeds())
	}
	a.state.Leds = api.Leds{Count: 100, Kept: 64}
	if a.chainLeds() != 64 {
		t.Fatalf("still sending to 64, %d", a.chainLeds())
	}
	a.state.Leds = api.Leds{Count: 100, Setting: 100}
	var ops op.Ops
	var router input.Router
	a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight))
	if len(a.tune.frame) != 100 || !strings.Contains(stripText(a.chainLeds()), "on 100 LEDs") {
		t.Fatalf("Tune draws %d LEDs", len(a.tune.frame))
	}
	const cell, gap = 42, 14
	if chainFit(1200, 33, cell, gap) != 1 {
		t.Fatal("32 LEDs drawn smaller than they need")
	}
	if scale := chainFit(1200, 101, cell, gap); scale >= 1 || scale < chainScales[len(chainScales)-1] {
		t.Fatalf("100 LEDs at %v", scale)
	}
	if chainNumberStep(56, 26) != 1 || chainNumberStep(20, 26) != 5 || chainNumberStep(4, 26) != 10 || chainNumberStep(1, 26) != 20 {
		t.Fatal("numbers run together, or are left out with room for them")
	}
}

func TestAnLedsRaysAreItsHueAndGrowWithItsLight(t *testing.T) {
	if light := ledLight(engine.Color{}); light != 0 {
		t.Fatalf("an LED off gives %v", light)
	}
	if dim, bright := ledLight(engine.Color{R: 40}), ledLight(engine.Color{R: 200, G: 10}); dim >= bright || bright > 1 {
		t.Fatalf("%v, %v", dim, bright)
	}
	if ink := rayInk(engine.Color{R: 64, G: 32}); ink != (color.NRGBA{R: 0xFF, G: 0x7F, A: 0xFF}) {
		t.Fatalf("%+v", ink)
	}
	if off := ledColor(engine.Color{}); off.R > 40 || off.G > 40 || off.B > 40 {
		t.Fatalf("an LED off is not dark: %+v", off)
	}
}

// The preview's time runs at its speed, and a change of speed goes on from where it is.
func TestThePreviewsTimeRunsAtItsSpeed(t *testing.T) {
	var clock previewClock
	start := time.Now()
	if at := clock.advance(start, 2); at != 0 {
		t.Fatalf("started at %d", at)
	}
	if at := clock.advance(start.Add(time.Second), 2); at != 2000 {
		t.Fatalf("a second at 2x reached %d", at)
	}
	if at := clock.advance(start.Add(3*time.Second), 0.5); at != 3000 {
		t.Fatalf("two more at 0.5x reached %d", at)
	}
	clock.restart()
	if at := clock.advance(start.Add(time.Hour), 1); at != 0 {
		t.Fatalf("started again at %d", at)
	}
}

// It starts at the garland's speed, or 1x with no garland to ask, and at full brightness whatever the
// garland's: one turned down low would show as dark.
func TestThePreviewStartsAtTheGarlandsSpeedAndFullBrightness(t *testing.T) {
	var look previewLook
	look.seed(api.State{})
	if look.pace() != 1 || look.percent() != 100 {
		t.Fatalf("with none: %v, %d%%", look.pace(), look.percent())
	}
	look.seed(api.State{Speed: 2.5, Brightness: 15})
	if look.pace() != 2.5 || look.percent() != 100 {
		t.Fatalf("with one at 15%%: %v, %d%%", look.pace(), look.percent())
	}
}

// With no garland, and with one turned down low, the preview's LEDs still light.
func TestThePreviewLightsWithNoGarlandAndWithADimOne(t *testing.T) {
	low := sceneApp(t, PageEffects)
	low.state.Brightness = 15
	for name, a := range map[string]*App{"no garland": sceneLocal(t), "at 15%": low} {
		a.preview.open(a, sceneEffect, "Aurora")
		if a.modal != modalPreview {
			t.Fatalf("%s: no preview: %s", name, a.status)
		}
		a.preview.look.clock.played = 3 * time.Second
		var ops op.Ops
		var router input.Router
		a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight))
		brightest := 0
		for _, led := range a.preview.frame {
			brightest = max(brightest, int(led.R), int(led.G), int(led.B))
		}
		if brightest < 120 {
			t.Errorf("%s: the brightest LED is %d", name, brightest)
		}
	}
}

// The window cannot be made smaller than this, and its pages and windows still fit.
func TestEverythingLaysOutAtTheSmallestWindow(t *testing.T) {
	width, height := int(windowMinWidth), int(windowMinHeight)
	scenes := map[string]*App{"tune": sceneTune(t), "preview": scenePreview(t), "connect": sceneConnect(t)}
	for page := PageEffects; page < pageCount; page++ {
		scenes[pageNames[page]] = sceneApp(t, page)
	}
	for name, a := range scenes {
		var ops op.Ops
		var router input.Router
		for range 2 {
			ops.Reset()
			if size := a.Frame(sceneContext(&ops, &router, width, height)); size.Size.X != width {
				t.Fatalf("%s is %d wide", name, size.Size.X)
			}
			router.Frame(&ops)
		}
	}
}
