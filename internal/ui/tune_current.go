// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tune's current: what the effect draws by the firmware's own model, and where the limit holds it back.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"image"
	"net/http"
	"time"

	"gioui.org/layout"
	"gioui.org/unit"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/engine"
)

const (
	currentSamples  = 200
	currentStepMs   = 100 // So the chart is 20 s of the effect's own time, as at 1x.
	currentWindowMs = currentSamples * currentStepMs
	// Worked out in one run when the file changes, so that each 20 s goes on from the one before, as the
	// preview does, and the engine the preview plays on is taken from it only then. Past them, the chart
	// starts again from the first.
	currentWindows = 10
	currentHigh    = unit.Dp(40)
	playheadWide   = unit.Dp(2)
)

// oldFirmwarePower is the model of a garland from before the power settings, 1.1.1 and earlier: 55 mA at
// full white, 1 mA dark, held to 1400 mA.
var oldFirmwarePower = engine.Power{SupplyMilliamps: 2000, WhiteMilliamps: 55, IdleMilliamps: 1, MarginPercent: 20}

// enginePower is the four settings of a garland's answer, as the engine takes them.
func enginePower(budget api.PowerBudget) engine.Power {
	return engine.Power{SupplyMilliamps: budget.SupplyMilliamps, WhiteMilliamps: budget.WhiteMilliamps, IdleMilliamps: budget.IdleMilliamps, MarginPercent: budget.MarginPercent}
}

// powerAskAgain is how long Tune waits before it asks again for settings it could not read.
const powerAskAgain = 3 * time.Second

// garlandPower says whose settings the engine's current is worked out by: those read from client, or none
// read yet from the garland connected to, whose numbers would be another's.
type garlandPower struct {
	client *api.Client
	known  bool
	asked  time.Time
}

// readGarlandPower has Tune work its current out as the garland connected to does: by its settings, or
// by the model of a release before them, which answers 404. A read that fails leaves them unknown for that
// garland, and Tune asks again. With none connected, the last ones stay, for work offline.
func (a *App) readGarlandPower() {
	a.power.asked = time.Now()
	a.background(func(ctx context.Context, client *api.Client) (func(), error) {
		budget, err := client.PowerBudget(ctx)
		var failure *api.Error
		switch {
		case err == nil:
			return func() { a.takeGarlandPower(client, enginePower(budget)) }, nil
		case errors.As(err, &failure) && failure.Status == http.StatusNotFound:
			return func() { a.takeGarlandPower(client, oldFirmwarePower) }, nil
		}
		return func() { a.power = garlandPower{client: client, asked: a.power.asked} }, fmt.Errorf("read the power settings: %w", err)
	})
}

func (a *App) takeGarlandPower(client *api.Client, power engine.Power) {
	_ = engine.SetPower(power)
	a.power = garlandPower{client: client, known: true, asked: a.power.asked}
}

// garlandPowerKnown is whether the engine's model is the connected garland's own: always, with none connected.
func (a *App) garlandPowerKnown() bool {
	return a.client == nil || (a.power.known && a.power.client == a.client)
}

// powerUnknownText says why Tune shows no current, asking again for the settings if it is time to.
func (a *App) powerUnknownText() string {
	if time.Since(a.power.asked) >= powerAskAgain {
		a.readGarlandPower()
	}
	return "The current is not shown until this garland's power settings are read: they could not be yet, and are asked for again."
}

// currentChart is what an effect draws at full brightness over its first 200 s, worked out again only when
// the file, the power settings or the LED count change: a brightness scales it, as the firmware's gain scales
// every channel.
type currentChart struct {
	file  string
	power engine.Power
	leds  int
	full  []int
	err   error
}

func (c *currentChart) update(file string, power engine.Power, leds int) {
	if file == c.file && power == c.power && leds == c.leds {
		return
	}
	c.file, c.power, c.leds = file, power, leds
	if file == "" {
		c.full, c.err = nil, nil
		return
	}
	times := make([]uint32, currentSamples*currentWindows)
	for i := range times {
		times[i] = uint32(i * currentStepMs)
	}
	c.full, c.err = engine.Currents([]byte(file), times, 100, leds)
}

// window is the 20 s that hold played, the effect's time in ms: where they start, and what the effect
// draws over them.
func (c *currentChart) window(played uint32) (uint32, []int) {
	from := ChartWindow(played)
	k := int(from/currentWindowMs) % currentWindows
	if len(c.full) < (k+1)*currentSamples {
		return from, nil
	}
	return from, c.full[k*currentSamples : (k+1)*currentSamples]
}

// ChartWindow is where the 20 s that hold played start, both in ms of the effect's own time.
func ChartWindow(played uint32) uint32 {
	return played / currentWindowMs * currentWindowMs
}

// PlayheadX is where, across a chart width wide that starts at from, the preview stands, played ms into
// the effect.
func PlayheadX(played, from uint32, width int) int {
	if played < from || width <= 0 {
		return 0
	}
	return min(int(uint64(played-from)*uint64(width)/currentWindowMs), width-1)
}

// ScaleCurrents is what currents drawn at full brightness come to at a share of it: what the LEDs draw
// dark, idle, stays, and only the rest scales.
func ScaleCurrents(full []int, share float64, idle int) []int {
	out := make([]int, len(full))
	for i, value := range full {
		out[i] = idle + int(float64(max(value-idle, 0))*share+0.5)
	}
	return out
}

// ChartTop is the current at the chart's top: the highest of the values and the limit, and a tenth above it,
// so that the limit's line stays in sight below the top edge.
func ChartTop(values []int, limit int) int {
	top := limit
	for _, value := range values {
		top = max(top, value)
	}
	return max(top+top/10, 1)
}

// CurrentText says what the effect draws against the limit: its peak, and how much of the time and by
// how much the garland holds it back.
// idle is what the LEDs draw dark, which the limiter leaves as it is, dimming only what is lit to the
// budget left beside it. The chart is sampled every 100 ms, so a peak between two samples is not in it.
func CurrentText(values []int, limit, idle, leds int) string {
	if len(values) == 0 {
		return ""
	}
	// The LEDs dark alone draw more than the limit: the garland keeps their rail off, as no dimming takes that
	// away, whatever the effect draws.
	if idle > limit {
		return fmt.Sprintf("%d LEDs draw %d mA dark alone, over the %d mA limit, which no dimming takes away, so the garland keeps them off: fewer LEDs, a lower idle draw, a bigger supply or a smaller margin on the Device page lets them on.", leds, idle, limit)
	}
	peak, over := 0, 0
	for _, value := range values {
		peak = max(peak, value)
		if value > limit {
			over++
		}
	}
	if over == 0 {
		return fmt.Sprintf("Sampled every 0.1 s at this brightness, over the 20 s shown, the effect draws up to %d mA on %d LEDs, under the %d mA limit, so it shows as drawn.", peak, leds, limit)
	}
	// The light, as the firmware's limiter dims it: the budget past the idle draw, over the lit part.
	dimmed := 100
	if peak > idle {
		dimmed = max(0, limit-idle) * 100 / (peak - idle)
	}
	return fmt.Sprintf("Sampled every 0.1 s at this brightness, over the 20 s shown, the effect would draw up to %d mA on %d LEDs, past the %d mA limit in %d%% of them: the garland dims those frames to about %d%% of their light or more.",
		peak, leds, limit, over*100/len(values), dimmed)
}

// currentRows are the chart, with the limit across it, the frames past it marked and a line where the
// preview plays, and its words. All three are of the frame the strip above has just drawn, and read when
// they are laid out, after it: so the line, the 20 s around it and what Now says are of what the LEDs show.
func (p *tunePage) currentRows(a *App) []layout.Widget {
	// Another garland's numbers would be taken for this one's.
	if !a.garlandPowerKnown() {
		if p.checked == "" {
			return nil
		}
		return []layout.Widget{a.hint(a.powerUnknownText())}
	}
	leds := a.chainLeds()
	p.current.update(p.checked, engine.PowerInUse(), leds)
	if p.current.err != nil || len(p.current.full) == 0 || p.preview == nil {
		return nil
	}
	idle := engine.IdleMilliamps(leds)
	limit := engine.LimitMilliamps()
	shown := func() (played, from uint32, values []int) {
		played = uint32(p.look.clock.played / time.Millisecond)
		from, full := p.current.window(played)
		return played, from, ScaleCurrents(full, engine.BrightnessShare(p.look.percent()), idle)
	}
	chart := func(gtx C) D {
		played, from, values := shown()
		if len(values) == 0 {
			return D{}
		}
		size := image.Pt(gtx.Constraints.Max.X, gtx.Dp(currentHigh))
		fill(gtx, size, 0, colorSurface)
		top := ChartTop(values, limit)
		height := func(value int) int { return size.Y * value / top }
		width := max(size.X/len(values), 1)
		line := size.Y - height(limit)
		for i, value := range values {
			x := i * size.X / len(values)
			h := max(height(value), 1)
			// Up to the limit as drawn, and past it in the limit's red: what the garland dims away.
			below := min(h, size.Y-line)
			offsetFill(gtx, image.Pt(x, size.Y-below), image.Pt(width, below), colorPrimary)
			if h > below {
				offsetFill(gtx, image.Pt(x, size.Y-h), image.Pt(width, h-below), colorDanger)
			}
		}
		offsetFill(gtx, image.Pt(0, line), image.Pt(size.X, max(gtx.Dp(unit.Dp(1)), 1)), colorDanger)
		// Where the preview plays, by the clock it is drawn by: the strip above advanced it this frame.
		offsetFill(gtx, image.Pt(PlayheadX(played, from, size.X), 0), image.Pt(max(gtx.Dp(playheadWide), 1), size.Y), colorStrong)
		return D{Size: size}
	}
	words := func(gtx C) D {
		_, _, values := shown()
		now := fmt.Sprintf("Now, at the white line: %d mA of the %d mA limit.", p.preview.Milliamps(), limit)
		return a.hint(CurrentText(values, limit, idle, leds) + " " + now)(gtx)
	}
	return []layout.Widget{chart, words}
}
