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
	// preview does. Past them, the chart starts again from the first.
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
	// Why the engine here refused the settings client's garland has, as of a range wider than its own: no
	// current is shown then, and they are not asked for again until Tune opens or the Device page reads them.
	refused error
}

// readGarlandPower has Tune work its current out as the garland connected to does: by its settings, or
// by the model of a release before them, which answers 404. A read that fails leaves them unknown for that
// garland, and Tune asks again. With none connected, the last ones stay, for work offline.
func (a *App) readGarlandPower() {
	a.power.asked = time.Now()
	a.background(func(ctx context.Context, client *api.Client) (func(), error) {
		budget, err := client.PowerBudget(ctx)
		var failure *api.Error
		power := oldFirmwarePower
		switch {
		case err == nil:
			power = enginePower(budget)
		case errors.As(err, &failure) && failure.Status == http.StatusNotFound:
		default:
			return func() { a.power = garlandPower{client: client, asked: a.power.asked} }, fmt.Errorf("read the power settings: %w", err)
		}
		// Not the garland's to answer for, so it is said here rather than taken for a network's failure.
		return func() {
			if err := a.takeGarlandPower(client, power); err != nil && client == a.client {
				a.reportError(err)
			}
		}, nil
	})
}

// takeGarlandPower has the engine work every current out by power, client's garland's. Settings the engine
// refuses leave them unknown for that garland, so that no current is shown by the ones before, another's.
func (a *App) takeGarlandPower(client *api.Client, power engine.Power) error {
	if err := engine.SetPower(power); err != nil {
		a.power = garlandPower{client: client, asked: a.power.asked, refused: err}
		return fmt.Errorf("Tune cannot work the current out by this garland's power settings: %w", err)
	}
	a.power = garlandPower{client: client, known: true, asked: a.power.asked}
	return nil
}

// garlandPowerKnown is whether the engine's model is the connected garland's own: always, with none connected.
func (a *App) garlandPowerKnown() bool {
	return a.client == nil || (a.power.known && a.power.client == a.client)
}

// powerUnknownText says why Tune shows no current, asking again for the settings if it is time to.
func (a *App) powerUnknownText() string {
	if a.power.refused != nil && a.power.client == a.client {
		return fmt.Sprintf("The current is not shown: this garland's power settings could not be used here (%v), and those of another garland would be wrong for it.", a.power.refused)
	}
	if time.Since(a.power.asked) >= powerAskAgain {
		a.readGarlandPower()
	}
	return "The current is not shown until this garland's power settings are read: they could not be yet, and are asked for again."
}

// currentChart is what an effect draws at full brightness over its first 200 s, worked out again only when
// the file, the power settings or the LED count change: a brightness scales it, as the firmware's gain scales
// every channel. It is worked out away from the window, on a run of the engine's own, which leaves the
// preview's as they are.
type currentChart struct {
	file  string
	power engine.Power
	leds  int
	full  []int
	err   error
	// What it is to be worked out for next, and since when: see follow.
	wanted      chartKey
	wantedSince time.Time
	// The run working one out, and how to stop it: nil with none. Counted, so that only the latest one's end
	// says none runs.
	stopRun context.CancelFunc
	runs    uint64
}

// chartKey is what a chart is worked out for.
type chartKey struct {
	file  string
	power engine.Power
	leds  int
}

// chartSettle is how long the file, the settings and the count stay as they are before the chart is worked out
// again for them: a slider dragged rewrites the file at every frame, and each working out runs the effect for
// 200 s on up to 256 LEDs.
const chartSettle = 300 * time.Millisecond

func (c *currentChart) key() chartKey { return chartKey{c.file, c.power, c.leds} }

// follow has the chart worked out for what is wanted once it has stood chartSettle, or at once with none worked
// out yet; until it comes the chart stays as it was, for what it was worked out for. A run for what is no
// longer wanted is stopped.
func (c *currentChart) follow(a *App, file string, power engine.Power, leds int, now time.Time) {
	wanted := chartKey{file, power, leds}
	if wanted == c.key() {
		c.wanted = wanted
		c.stop()
		return
	}
	if wanted != c.wanted {
		c.wanted, c.wantedSince = wanted, now
		c.stop()
	}
	if c.stopRun != nil || (c.full != nil && now.Sub(c.wantedSince) < chartSettle) {
		return
	}
	c.start(a, wanted)
}

// stale is whether the chart belongs to something other than what is wanted now: its curve and conclusion
// stay hidden until the new run arrives, as they would describe an earlier effect or model.
func (c *currentChart) stale() bool { return c.key() != c.wanted }

func (c *currentChart) stop() {
	if c.stopRun != nil {
		c.stopRun()
		c.stopRun = nil
	}
}

// reset forgets the chart, as for another effect, whose chart is not to be shown the old one's meanwhile.
func (c *currentChart) reset() {
	c.stop()
	*c = currentChart{runs: c.runs}
}

// start works the chart out for key away from the window, and takes it at a frame if it is still wanted then.
func (c *currentChart) start(a *App, key chartKey) {
	if key.file == "" {
		c.take(key, nil, nil)
		return
	}
	ctx, stop := context.WithCancel(a.lifetime)
	c.runs++
	run := c.runs
	c.stopRun = stop
	go func() {
		defer stop()
		full, err := chartCurrents(ctx, key)
		if ctx.Err() != nil {
			return
		}
		a.post(func() { c.arrive(run, key, full, err) })
	}()
}

// arrive takes what run worked out for key, unless something else is wanted by now.
func (c *currentChart) arrive(run uint64, key chartKey, full []int, err error) {
	if run == c.runs {
		c.stopRun = nil
	}
	if key != c.wanted {
		return
	}
	c.take(key, full, err)
}

func (c *currentChart) take(key chartKey, full []int, err error) {
	c.file, c.power, c.leds = key.file, key.power, key.leds
	c.full, c.err = full, err
}

// chartCurrents is what the effect of key draws over the run of the chart, at full brightness.
func chartCurrents(ctx context.Context, key chartKey) ([]int, error) {
	times := make([]uint32, currentSamples*currentWindows)
	for i := range times {
		times[i] = uint32(i * currentStepMs)
	}
	return engine.Currents(ctx, []byte(key.file), times, 100, key.leds, key.power)
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

// text says what a fresh chart means, or that its replacement is being worked out. The preview's current
// and limit are current in both cases; no older samples are described by that model while stale.
func (c *currentChart) text(values []int, limit, idle, now int) string {
	if c.stale() {
		return fmt.Sprintf("Recalculating the current chart. Now in the preview: %d mA of the %d mA limit.", now, limit)
	}
	return CurrentText(values, limit, idle, c.leds) + " " +
		fmt.Sprintf("Now, at the white line: %d mA of the %d mA limit.", now, limit)
}

// currentRows are the chart, with the limit across it, the frames past it marked and a line where the
// preview plays, and its words. The line and what Now says are of the frame the strip above has just drawn,
// read when they are laid out, after it; the bars are of what the chart was last worked out for, which a
// change reaches only once it has settled and its run has ended, hidden until then.
func (p *tunePage) currentRows(a *App, now time.Time) []layout.Widget {
	// Another garland's numbers would be taken for this one's.
	if !a.garlandPowerKnown() {
		if p.checked == "" {
			return nil
		}
		return []layout.Widget{a.hint(a.powerUnknownText())}
	}
	p.current.follow(a, p.checked, engine.PowerInUse(), a.chainLeds(), now)
	if p.preview == nil {
		return nil
	}
	limit := engine.LimitMilliamps()
	if p.current.stale() {
		// Retain the samples for follow's debounce, but neither scale them by another model's idle draw nor
		// compare them with another limit. Now is read after the preview draws, without a chart's white line.
		return []layout.Widget{func(gtx C) D {
			return a.hint(p.current.text(nil, limit, 0, p.preview.Milliamps()))(gtx)
		}}
	}
	if p.current.err != nil || len(p.current.full) == 0 {
		return nil
	}
	// A fresh chart's count and model match the preview's, so its samples, idle draw and limit belong together.
	leds := p.current.leds
	idle := engine.IdleMilliamps(leds)
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
			under := min(h, size.Y-line)
			offsetFill(gtx, image.Pt(x, size.Y-under), image.Pt(width, under), colorPrimary)
			if h > under {
				offsetFill(gtx, image.Pt(x, size.Y-h), image.Pt(width, h-under), colorDanger)
			}
		}
		offsetFill(gtx, image.Pt(0, line), image.Pt(size.X, max(gtx.Dp(unit.Dp(1)), 1)), colorDanger)
		// Where the preview plays, by the clock it is drawn by: the strip above advanced it this frame.
		offsetFill(gtx, image.Pt(PlayheadX(played, from, size.X), 0), image.Pt(max(gtx.Dp(playheadWide), 1), size.Y), colorStrong)
		return D{Size: size}
	}
	words := func(gtx C) D {
		_, _, values := shown()
		return a.hint(p.current.text(values, limit, idle, p.preview.Milliamps()))(gtx)
	}
	return []layout.Widget{chart, words}
}
