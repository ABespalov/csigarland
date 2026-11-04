// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Schedule page's next 24 hours: lit or dark as the actions leave it, and where actions meet.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"image"
	"image/color"
	"math"
	"net/http"
	"time"

	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/unit"

	"github.com/ABespalov/csigarland/internal/api"
)

const (
	dayMinutes   = 24 * 60
	timelineHigh = unit.Dp(22)
	timelineMark = unit.Dp(3)
)

var weekdayNames = [7]string{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}

// Span is a stretch of the next 24 hours, in minutes from now, when the garland is lit.
type Span struct{ From, To int }

// MinuteOfEvent is how many minutes from now, at unix second now, an event comes, within the day.
func MinuteOfEvent(event api.TimelineEvent, now int64) int {
	return min(max(int((event.At-now+30)/60), 0), dayMinutes)
}

// Upcoming leaves out the events past at unix second now: whether the garland is lit says what they did,
// and anything the owner did since, as the page may stay open long after they came. Those from unix second
// from on stay too: the garland gives a step its scheduler has yet to do, of a minute begun or a few it
// catches up, while the state the day is drawn from is of before it, and it is drawn from the start of the
// day. The caller gives from as now once a state newer than the day's reading came, which says what they
// did, or as the earliest second there is while none has.
func Upcoming(events []api.TimelineEvent, now, from int64) []api.TimelineEvent {
	var left []api.TimelineEvent
	for _, event := range events {
		if event.At >= now || event.At >= from {
			left = append(left, event)
		}
	}
	return left
}

// PendingFrom is the second from which the events a day's reading gave still count, as Upcoming takes it.
// The state's own word on how far the scheduler has done its steps, stepsDone, decides: those before it are
// done, whatever the power came to since, as a step done and a choice undoing it leave the power as it was.
// Without it, as from an earlier build, with the garland's word on whether it was lit as the day was worked
// out, they count until the power the window holds is another than that; with neither, until any newer
// state came.
func PendingFrom(now int64, read, stateAt time.Time, dayPower *bool, power bool, stepsDone int64) int64 {
	switch {
	case stepsDone > 0:
		return stepsDone
	case dayPower != nil && *dayPower == power:
		return math.MinInt64
	case dayPower != nil, stateAt.After(read):
		return now
	}
	return math.MinInt64
}

// LitSpans works out when the garland is lit over the next 24 hours, from whether it is lit now and
// what the actions still to come do: an off makes it dark, an on, a rise or an effect lit, and anything
// else leaves it.
func LitSpans(events []api.TimelineEvent, litNow bool, now, pendingFrom int64) []Span {
	var spans []Span
	lit, from := litNow, 0
	for _, event := range Upcoming(events, now, pendingFrom) {
		next := lit
		switch {
		case event.Power == api.StepOff:
			next = false
		case event.Power != "" || event.Effect != "" || event.Playlist:
			next = true
		}
		if next == lit {
			continue
		}
		at := MinuteOfEvent(event, now)
		if lit {
			spans = append(spans, Span{from, at})
		}
		lit, from = next, at
	}
	if lit {
		spans = append(spans, Span{from, dayMinutes})
	}
	return spans
}

// TimelineText says what one event does, and that actions meet there if they do.
func TimelineText(event api.TimelineEvent, effectName func(string) string) string {
	effect := ""
	if event.Effect != "" {
		effect = effectName(event.Effect)
	}
	day := "?"
	if event.Day >= 0 && event.Day < len(weekdayNames) {
		day = weekdayNames[event.Day]
	}
	words := fmt.Sprintf("%s %s: %s", day, event.Time, StepsText(event.Power, event.Brightness, event.Speed, effect, event.Playlist))
	if len(event.Entries) > 1 {
		words += fmt.Sprintf(" (%d actions meet here)", len(event.Entries))
	}
	return words
}

// The next 24 hours are asked for again after this, so a page left open keeps its horizon a day ahead,
// and sooner after a reading that failed.
const (
	timelineFresh = 5 * time.Minute
	timelineRetry = 30 * time.Second
)

// loadTimeline asks for the next 24 hours after a change that moves them, a list saved or a clock set:
// a reading on its way is older, and is asked again once it comes.
func (p *schedulePage) loadTimeline(a *App) {
	p.timelineGen++
	p.timelineNone = false
	p.askTimeline(a)
}

// keepTimeline asks again as the day moves on, while the page shows, whatever the card shows meanwhile.
func (p *schedulePage) keepTimeline(a *App) {
	wait := timelineFresh
	if p.timelineFailed {
		wait = timelineRetry
	}
	if !p.timelineNone && time.Since(p.timelineTried) >= wait {
		p.askTimeline(a)
	}
}

// askTimeline reads the next 24 hours, apart from the rest. A garland of 1.0.0 has no such route, says
// so with 404, and is not asked again; any other failure keeps what was read, and is tried again soon.
func (p *schedulePage) askTimeline(a *App) {
	if p.timelineAsked {
		return
	}
	p.timelineAsked = true
	gen := p.timelineGen
	a.background(func(ctx context.Context, client *api.Client) (func(), error) {
		timeline, err := client.Timeline(ctx)
		// The day's own time is when it came, not when the clock read after it does.
		readAt := time.Now()
		// The clock with it, which the card says the day goes by: read once with the page, it would still
		// say the clock is not set after the first sync, beside the day it brought.
		var clock api.Clock
		clockErr := err
		if err == nil {
			clock, clockErr = client.Clock(ctx)
		}
		return func() {
			p.timelineAsked = false
			if gen != p.timelineGen {
				p.askTimeline(a)
				return
			}
			// The day's own time moves only with a day read: a reading that failed keeps the one shown,
			// and the clock it is drawn by.
			p.timelineTried = time.Now()
			var failure *api.Error
			switch {
			case err == nil:
				p.timeline, p.timelineFailed, p.timelineAt = &timeline, false, readAt
				switch {
				case clockErr == nil:
					p.clock = clock
				case timeline.Synced && !p.clock.Synced:
					// The day says the clock is set, which the clock failing to read takes nothing from.
					p.clock.Synced, p.clock.Now = true, ""
				}
			case errors.As(err, &failure) && failure.Status == http.StatusNotFound && !api.HasRoute(a.about.Version, "schedule/timeline"):
				// A garland of 1.0.0 has no day to show; one of a release with it is asked again.
				p.timeline, p.timelineNone = nil, true
			default:
				p.timelineFailed = true
			}
		}, nil
	})
}

func (p *schedulePage) timelineCard(a *App) layout.Widget {
	return func(gtx C) D {
		timeline := p.timeline
		if !timeline.Synced {
			return a.card(gtx, "The next 24 hours", a.hint(p.clockText()))
		}
		// From the garland's clock, which the events are of, as this computer's may differ, less the
		// time since the answer came.
		now := time.Now().Unix()
		if timeline.Now > 0 {
			now = timeline.Now + int64(time.Since(p.timelineAt).Seconds())
		}

		from := PendingFrom(now, p.timelineAt, a.stateAt, timeline.Power, a.state.Power, a.state.StepsDoneUntil)
		events := Upcoming(timeline.Events, now, from)
		bar := func(gtx C) D {
			size := image.Pt(gtx.Constraints.Max.X, gtx.Dp(timelineHigh))
			fill(gtx, size, 0, colorQuiet)
			at := func(minute int) int { return size.X * minute / dayMinutes }
			for _, span := range LitSpans(timeline.Events, a.state.Power, now, from) {
				offsetFill(gtx, image.Pt(at(span.From), 0), image.Pt(max(at(span.To)-at(span.From), 1), size.Y), colorPrimary)
			}
			for _, event := range events {
				mark := colorStrong
				if len(event.Entries) > 1 {
					mark = colorActive
				}
				offsetFill(gtx, image.Pt(at(MinuteOfEvent(event, now)), 0), image.Pt(gtx.Dp(timelineMark), size.Y), mark)
			}
			return D{Size: size}
		}
		rows := []layout.Widget{
			a.hint(p.clockText()),
			a.hint("Lit or dark as the actions leave it, from now. Where actions meet in one minute, the later in the list win, step by step."),
			bar,
		}
		for _, event := range events {
			rows = append(rows, a.body(TimelineText(event, func(id string) string { return p.effectName(api.Action{Effect: id}) })))
		}
		if len(events) == 0 {
			rows = append(rows, a.hint("Nothing in the next 24 hours."))
		}
		return a.card(gtx, "The next 24 hours", rows...)
	}
}

// A rectangle of colour at a place, where fill draws at the origin.
func offsetFill(gtx C, at, size image.Point, paint color.NRGBA) {
	defer op.Offset(at).Push(gtx.Ops).Pop()
	fill(gtx, size, 0, paint)
}
