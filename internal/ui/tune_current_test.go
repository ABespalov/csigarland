// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of Tune's current, told against the limit, and of the API check's words.
// Contributors: None

package ui

import (
	"context"
	"fmt"
	"strings"
	"testing"
	"time"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/engine"
)

// fillChart works the chart out here, as a test has no window for follow to post back to.
func fillChart(t *testing.T, chart *currentChart, file string, power engine.Power, leds int) {
	t.Helper()
	key := chartKey{file, power, leds}
	if chart.key() == key && chart.full != nil {
		return
	}
	full, err := chartCurrents(context.Background(), key)
	if err != nil {
		t.Fatal(err)
	}
	chart.take(key, full, nil)
}

func TestTheCurrentIsToldAgainstTheLimit(t *testing.T) {
	if got := ScaleCurrents([]int{1000, 2000}, 0.5, 0); got[0] != 500 || got[1] != 1000 {
		t.Fatalf("%v", got)
	}
	// What the LEDs draw dark stays at any brightness.
	if got := ScaleCurrents([]int{1032, 32}, 0.5, 32); got[0] != 532 || got[1] != 32 {
		t.Fatalf("%v", got)
	}
	under := CurrentText([]int{300, 900, 1200}, 1400, 32, 32)
	if !strings.Contains(under, "up to 1200 mA") || !strings.Contains(under, "shows as drawn") || !strings.Contains(under, "Sampled") {
		t.Fatalf("%q", under)
	}
	// 32 LEDs at full white, 1792 mA with 32 mA idle: the lit part gets (1400 - 32) / (1792 - 32).
	over := CurrentText([]int{1000, 1792, 1792, 1300}, 1400, 32, 32)
	if !strings.Contains(over, "up to 1792 mA") || !strings.Contains(over, "in 50%") || !strings.Contains(over, "about 77%") {
		t.Fatalf("%q", over)
	}
	// 64 LEDs at 5 mA dark, 320 mA, over a limit of 200: kept off, for a dark effect as for a lit one.
	for _, values := range [][]int{{320, 320}, {320, 2880}} {
		if words := CurrentText(values, 200, 320, 64); !strings.Contains(words, "64 LEDs draw 320 mA dark alone") || !strings.Contains(words, "keeps them off") || strings.Contains(words, "dims") {
			t.Fatalf("%q", words)
		}
	}
	if CurrentText(nil, 1400, 32, 32) != "" {
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
	fillChart(t, &chart, white, engine.PowerInUse(), previewLeds)
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
	fillChart(t, &chart, white, engine.PowerInUse(), previewLeds)
	if chart.full[0] != 1792 || chart.full[0] <= engine.LimitMilliamps() {
		t.Fatalf("by the old model white draws %d mA", chart.full[0])
	}
	if low := ScaleCurrents(chart.full, engine.BrightnessShare(25), engine.IdleMilliamps(previewLeds)); low[0] >= engine.LimitMilliamps() {
		t.Fatalf("at 25%% white still draws %d mA", low[0])
	}
	// Worked out again for a garland of another length: 100 LEDs at 55 + 1 mA.
	fillChart(t, &chart, white, engine.PowerInUse(), 100)
	if chart.full[0] != 5600 || !strings.Contains(CurrentText(chart.full[:1], 1400, 100, 100), "on 100 LEDs") {
		t.Fatalf("100 LEDs draw %d mA", chart.full[0])
	}
	// The limit's line stays under the chart's top, whatever is past it.
	if top := ChartTop([]int{300, 900}, 1400); top <= 1400 {
		t.Fatalf("the limit is at the top: %d", top)
	}
	if top := ChartTop([]int{5600}, 1400); top <= 5600 {
		t.Fatalf("the highest bar is at the top: %d", top)
	}
	budget := api.PowerBudget{SupplyMilliamps: 4000, WhiteMilliamps: 36, IdleMilliamps: 1, MarginPercent: 10, LimitMilliamps: 3375}
	if enginePower(budget) != (engine.Power{SupplyMilliamps: 4000, WhiteMilliamps: 36, IdleMilliamps: 1, MarginPercent: 10}) {
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
	fillChart(t, &chart, file, engine.PowerInUse(), previewLeds)
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
	fillChart(t, &chart, file, engine.PowerInUse(), previewLeds)
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

// A change of the file is worked out once it has stood a moment, not at each frame of a slider dragged: the chart
// keeps its samples meanwhile, hidden until replaced; with none yet, it is worked out at once.
func TestTheChartWaitsForTheFileToSettle(t *testing.T) {
	white := `{"format":"csigarland.effect/1","name":"W","layers":[{"fx":"solid","col":["FFFFFF"]}]}`
	red := `{"format":"csigarland.effect/1","name":"R","layers":[{"fx":"solid","col":["FF0000"]}]}`
	var chart currentChart
	ctx, stop := context.WithCancel(context.Background())
	t.Cleanup(stop)
	app := &App{lifetime: ctx, queue: make(chan func(), 4), invalidate: func() {}}
	start := time.Now()
	power := engine.PowerInUse()
	chart.follow(app, white, power, previewLeds, start)
	waitForChart(t, app, &chart, white)
	first := chart.full
	chart.follow(app, red, power, previewLeds, start.Add(10*time.Millisecond))
	chart.follow(app, red, power, previewLeds, start.Add(chartSettle))
	app.drain()
	if chart.file != white || &chart.full[0] != &first[0] {
		t.Fatal("worked out again before the file settled")
	}
	chart.follow(app, red, power, previewLeds, start.Add(10*time.Millisecond+chartSettle))
	waitForChart(t, app, &chart, red)
	if chart.full[0] >= first[0] {
		t.Fatalf("not worked out once settled: %d", chart.full[0])
	}
	// A count changed is waited for too, and taken.
	chart.follow(app, red, power, 64, start.Add(time.Second))
	chart.follow(app, red, power, 64, start.Add(time.Second+chartSettle))
	waitForChart(t, app, &chart, red)
	if chart.leds != 64 {
		t.Fatalf("the count of %d stays", chart.leds)
	}
}

// A retained chart is hidden, including its conclusion, until it matches the preview again. The preview's
// instantaneous demand still follows the current model, rather than the samples kept for the debounce.
func TestTuneHidesTheEarlierModelsChartWhileRecalculating(t *testing.T) {
	before := engine.PowerInUse()
	t.Cleanup(func() { _ = engine.SetPower(before) })
	file := `{"format":"csigarland.effect/1","name":"W","layers":[{"fx":"solid","col":["FFFFFF"]}]}`
	for _, test := range []struct {
		name       string
		power      engine.Power
		leds       int
		idle       int
		limit      int
		peak       int
		conclusion string
	}{
		{"white draw", engine.Power{SupplyMilliamps: 2000, WhiteMilliamps: 100, IdleMilliamps: 1, MarginPercent: 20}, 32, 32, 1400, 3232, "the garland dims"},
		{"idle, limit and count", engine.Power{SupplyMilliamps: 500, WhiteMilliamps: 40, IdleMilliamps: 5, MarginPercent: 20}, 64, 320, 200, 2880, "64 LEDs draw 320 mA dark alone"},
	} {
		t.Run(test.name, func(t *testing.T) {
			original := engine.Power{SupplyMilliamps: 2000, WhiteMilliamps: 40, IdleMilliamps: 1, MarginPercent: 20}
			if err := engine.SetPower(original); err != nil {
				t.Fatal(err)
			}
			preview, err := engine.NewPreview([]byte(file))
			if err != nil {
				t.Fatal(err)
			}
			page := tunePage{checked: file, preview: preview}
			fillChart(t, &page.current, file, original, 32)
			app := &App{client: &api.Client{}}
			app.power = garlandPower{client: app.client, known: true}
			app.state.Leds.Count = 32
			at := time.Now()
			if rows := page.currentRows(app, at); len(rows) != 2 || page.current.full[0] != 1312 {
				t.Fatal("the original full-white chart is not shown")
			}
			if err := engine.SetPower(test.power); err != nil {
				t.Fatal(err)
			}
			app.state.Leds.Count = test.leds
			if rows := page.currentRows(app, at.Add(time.Millisecond)); len(rows) != 1 || !page.current.stale() {
				t.Fatal("the earlier model's curve is still shown while its replacement settles")
			}
			preview.Frame(0, 100, make([]engine.Color, test.leds))
			if preview.Milliamps() != test.peak || engine.LimitMilliamps() != test.limit || engine.IdleMilliamps(test.leds) != test.idle {
				t.Fatalf("current model: %d mA, limit %d, idle %d", preview.Milliamps(), engine.LimitMilliamps(), engine.IdleMilliamps(test.leds))
			}
			words := page.current.text(page.current.full[:1], test.limit, test.idle, preview.Milliamps())
			instant := fmt.Sprintf("Now in the preview: %d mA of the %d mA limit.", test.peak, test.limit)
			if !strings.Contains(words, "Recalculating the current chart") || !strings.Contains(words, instant) ||
				strings.Contains(words, "1312") || strings.Contains(words, "shows as drawn") || strings.Contains(words, "dims") || strings.Contains(words, "white line") {
				t.Fatalf("an earlier conclusion was shown while recalculating: %q", words)
			}
			if page.current.full[0] != 1312 {
				t.Fatal("the debounce no longer retains the earlier samples")
			}
			// A completed replacement brings the curve and its conclusion back together, using the new count,
			// idle draw and limit; neither the old under-limit conclusion nor the recalculation label stays.
			fillChart(t, &page.current, file, test.power, test.leds)
			if rows := page.currentRows(app, at.Add(2*time.Millisecond)); len(rows) != 2 || page.current.stale() {
				t.Fatal("the replacement chart is not shown")
			}
			words = page.current.text(page.current.full[:1], test.limit, test.idle, preview.Milliamps())
			if page.current.full[0] != test.peak || !strings.Contains(words, test.conclusion) || strings.Contains(words, "Recalculating") || strings.Contains(words, "shows as drawn") {
				t.Fatalf("the replacement's conclusion is wrong: %q", words)
			}
		})
	}
}

func waitForChart(t *testing.T, app *App, chart *currentChart, file string) {
	t.Helper()
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		app.drain()
		if chart.file == file && chart.stopRun == nil && len(chart.full) > 0 {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("the chart for the file was not worked out")
}
