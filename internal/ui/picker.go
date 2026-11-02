// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Where picking the clock module's time starts, and the arrows a stepper shares.
// Contributors: None

package ui

import (
	"image"
	"time"

	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
)

const (
	// The years the firmware takes a time for.
	pickerFirstYear = 2026
	pickerLastYear  = 2099
	// What POST /api/v1/time/rtc takes as local, the garland's own wall clock.
	pickerLayout   = "2006-01-02T15:04"
	pickerHeight   = unit.Dp(36)
	pickerArrows   = unit.Dp(20)
	pickerPad      = unit.Dp(8)
	pickerArrow    = unit.Dp(12)
	pickerCorner   = unit.Dp(6)
	pickerMaxWheel = 1 << 20
)

// pickerControl is a stepper's arrows, and the tag its mouse wheel is read under.
type pickerControl struct {
	up, down widget.Clickable
}

// startTime is where picking the module's time starts: its own time if that is to be trusted, else
// the garland's, else this computer's, to the minute. The garland takes the time in its own zone, so
// the numbers are held in UTC only as a place to keep them.
func startTime(clock api.Clock, now time.Time) time.Time {
	at := time.Date(now.Year(), now.Month(), now.Day(), now.Hour(), now.Minute(), 0, 0, time.UTC)
	var known []string
	if clock.Module.Trusted {
		known = append(known, clock.Module.Now)
	}
	if clock.Synced {
		known = append(known, clock.Now)
	}
	found := false
	for _, said := range known {
		for _, layout := range []string{"2006-01-02 15:04:05", "2006-01-02 15:04", "2006-01-02T15:04:05", pickerLayout} {
			if parsed, err := time.Parse(layout, said); err == nil && !found {
				at, found = parsed.Truncate(time.Minute), true
			}
		}
	}
	first := time.Date(pickerFirstYear, time.January, 1, 0, 0, 0, 0, time.UTC)
	last := time.Date(pickerLastYear, time.December, 31, 23, 59, 0, 0, time.UTC)
	switch {
	case at.Before(first):
		return first
	case at.After(last):
		return last
	}
	return at
}
func (a *App) pickerArrow(gtx C, click *widget.Clickable, glyph string, at, size image.Point) {
	moved := op.Offset(at).Push(gtx.Ops)
	defer moved.Pop()
	over := a.hovered(gtx, click)
	place := gtx
	place.Constraints = layout.Exact(size)
	click.Layout(place, func(gtx C) D {
		if over {
			paint.FillShape(gtx.Ops, colorQuiet, clip.Rect{Max: size}.Op())
		}
		drawn := gtx.Dp(pickerArrow)
		inner := op.Offset(image.Pt((size.X-drawn)/2, (size.Y-drawn)/2)).Push(gtx.Ops)
		drawGlyph(gtx, glyph, drawn, colorText)
		inner.Pop()
		return D{Size: size}
	})
	a.watchHover(gtx, click, size)
}
