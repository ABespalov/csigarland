// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of Tune's current, told against the limit, and of the API check's words.
// Contributors: None

package ui

import (
	"strings"
	"testing"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/engine"
)

func TestTheCurrentIsToldAgainstTheLimit(t *testing.T) {
	if got := ScaleCurrents([]int{1000, 2000}, 0.5, 0); got[0] != 500 || got[1] != 1000 {
		t.Fatalf("%v", got)
	}
	// What the LEDs draw dark stays at any brightness.
	if got := ScaleCurrents([]int{1032, 32}, 0.5, 32); got[0] != 532 || got[1] != 32 {
		t.Fatalf("%v", got)
	}
	under := CurrentText([]int{300, 900, 1200}, 1400, 32)
	if !strings.Contains(under, "up to 1200 mA") || !strings.Contains(under, "shows as drawn") || !strings.Contains(under, "Sampled") {
		t.Fatalf("%q", under)
	}
	// 32 LEDs at full white, 1792 mA with 32 mA idle: the lit part gets (1400 - 32) / (1792 - 32).
	over := CurrentText([]int{1000, 1792, 1792, 1300}, 1400, 32)
	if !strings.Contains(over, "up to 1792 mA") || !strings.Contains(over, "in 50%") || !strings.Contains(over, "about 77%") {
		t.Fatalf("%q", over)
	}
	if CurrentText(nil, 1400, 32) != "" {
		t.Fatal("no frames, yet words")
	}
	if engine.IdleMilliamps(-5) != 0 {
		t.Fatal("a chain of no LEDs draws something")
	}
}

// Tune's chart is the engine's, at full brightness, by the garland's power settings, and scales down with
// the slider.
func TestTuneChartsTheEffectsCurrent(t *testing.T) {
	defaults := engine.DefaultPower()
	t.Cleanup(func() { _ = engine.SetPower(defaults) })
	white := `{"format":"csigarland.effect/1","name":"W","layers":[{"fx":"solid","col":["FFFFFF"]}]}`
	var chart currentChart
	chart.update(white, engine.PowerInUse())
	if engine.IdleMilliamps(previewLeds) <= 0 {
		t.Fatal("the LEDs draw nothing dark, by the model")
	}
	// 32 LEDs at 40 + 1 mA stay under the default 1400 mA.
	if chart.err != nil || len(chart.full) != currentSamples*currentWindows || chart.full[0] != 1312 {
		t.Fatalf("%v %d", chart.err, len(chart.full))
	}
	// A garland from before the settings: 55 + 1 mA, past the limit, worked out again for them.
	if err := engine.SetPower(oldFirmwarePower); err != nil {
		t.Fatal(err)
	}
	chart.update(white, engine.PowerInUse())
	if chart.full[0] != 1792 || chart.full[0] <= engine.LimitMilliamps() {
		t.Fatalf("by the old model white draws %d mA", chart.full[0])
	}
	if low := ScaleCurrents(chart.full, engine.BrightnessShare(25), engine.IdleMilliamps(previewLeds)); low[0] >= engine.LimitMilliamps() {
		t.Fatalf("at 25%% white still draws %d mA", low[0])
	}
	budget := api.PowerBudget{SupplyMilliamps: 5000, WhiteMilliamps: 36, IdleMilliamps: 1, MarginPercent: 10, LimitMilliamps: 4300}
	if enginePower(budget) != (engine.Power{SupplyMilliamps: 5000, WhiteMilliamps: 36, IdleMilliamps: 1, MarginPercent: 10}) {
		t.Fatalf("%+v", enginePower(budget))
	}
}

// The chart is the 20 s the preview plays in, and its line goes across it with the effect's own time.
func TestTheChartFollowsThePreview(t *testing.T) {
	if ChartWindow(0) != 0 || ChartWindow(19999) != 0 || ChartWindow(20000) != 20000 || ChartWindow(45000) != 40000 {
		t.Fatalf("%d %d %d", ChartWindow(19999), ChartWindow(20000), ChartWindow(45000))
	}
	if PlayheadX(0, 0, 400) != 0 || PlayheadX(10000, 0, 400) != 200 || PlayheadX(45000, 40000, 400) != 100 || PlayheadX(59999, 40000, 400) != 399 {
		t.Fatalf("%d %d %d", PlayheadX(10000, 0, 400), PlayheadX(45000, 40000, 400), PlayheadX(59999, 40000, 400))
	}
	// Played before the chart's start, as for a frame before it is worked out again: at its start.
	if PlayheadX(100, 20000, 400) != 0 {
		t.Fatal("a line before the chart")
	}
	file := `{"format":"csigarland.effect/1","name":"W","layers":[{"fx":"solid","col":["FFFFFF"]}]}`
	var chart currentChart
	chart.update(file, engine.PowerInUse())
	run := chart.full
	from, first := chart.window(5000)
	next, second := chart.window(25000)
	again, wrapped := chart.window(currentWindows*currentWindowMs + 5000)
	if from != 0 || next != 20000 || len(first) != currentSamples || len(second) != currentSamples || &second[0] != &run[currentSamples] {
		t.Fatal("the next 20 s are not the run's own")
	}
	if again != currentWindows*currentWindowMs || &wrapped[0] != &first[0] {
		t.Fatal("past the run, the chart does not start again from the first")
	}
	// Played on, the chart is not worked out again, which would take the engine from the preview.
	chart.update(file, engine.PowerInUse())
	if &chart.full[0] != &run[0] {
		t.Fatal("worked out again with the same file")
	}
}

func TestTheAPICheckIsToldInOneLine(t *testing.T) {
	words, failed := APICheckText([]api.RouteCheck{{Route: "about"}, {Route: "display", Unknown: "glow"}})
	if failed || !strings.Contains(words, "all 2 routes") || !strings.Contains(words, "display (glow)") {
		t.Fatalf("%q %v", words, failed)
	}
	words, failed = APICheckText([]api.RouteCheck{{Route: "about"}, {Route: "state", Problem: "power is a string, read as bool"}})
	if !failed || !strings.Contains(words, "1 of 2 routes wrong: state: power is a string") {
		t.Fatalf("%q %v", words, failed)
	}
}
