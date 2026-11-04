// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Picking a time on wheels: how a wheel turns, where the clock's picking starts, its days.
// Contributors: None

package ui

import (
	"image"
	"strings"
	"testing"
	"time"

	"gioui.org/f32"
	"gioui.org/io/input"
	"gioui.org/io/pointer"
	"gioui.org/op"

	"github.com/ABespalov/csigarland/internal/api"
)

func TestAWheelTurnsRoundOrStopsAtItsEnds(t *testing.T) {
	var minutes wheel
	minutes.set(58, 0, 59, true, 2)
	minutes.step(3)
	if minutes.value != 1 {
		t.Errorf("58 and 3 more is %d", minutes.value)
	}
	minutes.step(-2)
	if minutes.value != 59 {
		t.Errorf("1 and 2 back is %d", minutes.value)
	}
	var years wheel
	years.set(2098, pickerFirstYear, pickerLastYear, false, 4)
	years.step(5)
	if years.value != pickerLastYear {
		t.Errorf("past the last year: %d", years.value)
	}
	if _, ok := years.at(1); ok {
		t.Error("a year after the last is shown")
	}
}

func TestPickingTheModulesTimeStartsAtATimeItCanTrust(t *testing.T) {
	now := time.Date(2026, 11, 2, 9, 30, 45, 0, time.Local)
	for _, start := range []struct {
		clock api.Clock
		want  string
	}{
		{api.Clock{Synced: true, Now: "2026-11-02 15:04", Module: api.ClockModule{Trusted: true, Now: "2026-11-02 15:41:02"}}, "2026-11-02 15:41"},
		{api.Clock{Synced: true, Now: "2026-11-02 15:04", Module: api.ClockModule{Now: "2000-01-01 00:00:00"}}, "2026-11-02 15:04"},
		{api.Clock{Now: "1970-01-01 00:00"}, "2026-11-02 09:30"},
	} {
		if got := startTime(start.clock, now).Format(clockTyped); got != start.want {
			t.Errorf("%+v: %s, want %s", start.clock, got, start.want)
		}
	}
	if at, ok := parseClockTyped(" 2027-02-28 07:05 "); !ok || at.Format(pickerLayout) != "2027-02-28T07:05" {
		t.Errorf("%v %v", at, ok)
	}
	for _, bad := range []string{"", "2027-02-30 07:05", "2025-12-31 23:59", "18:00"} {
		if _, ok := parseClockTyped(bad); ok {
			t.Errorf("%q was taken", bad)
		}
	}
}

// A day is kept to its month: the 31st of January in February is its last day.
func TestTheClocksDayIsKeptToItsMonth(t *testing.T) {
	var c clockPopup
	c.open(time.Date(2028, time.January, 31, 10, 0, 0, 0, time.UTC))
	c.month.step(1)
	c.fitDay()
	if got := c.value().Format(clockTyped); got != "2028-02-29 10:00" {
		t.Fatalf("got %s", got)
	}
	c.year.step(1)
	c.fitDay()
	if c.day.value != 28 || c.day.high != 28 {
		t.Fatalf("day %d of %d", c.day.value, c.day.high)
	}
}

// A wheel turns by the mouse wheel, by a drag that carries its numbers, and by a press on a neighbour.
func TestAWheelTurnsAsAPhonesDoes(t *testing.T) {
	a := sceneApp(t, PageSchedule)
	var w wheel
	w.set(30, 0, 59, true, 2)
	var router input.Router
	var gtx C
	draw := func() {
		var ops op.Ops
		a.frames++
		gtx = sceneContext(&ops, &router, sceneWidth, sceneHeight)
		gtx.Constraints.Min = image.Point{}
		a.source = gtx.Source
		a.wheels(gtx, nil, wheelPart{wheel: &w})
		router.Frame(&ops)
	}
	draw()
	middle := wheelRowMiddle(gtx, 0)
	router.Queue(pointer.Event{Kind: pointer.Scroll, Source: pointer.Mouse, Position: middle, Scroll: f32.Pt(0, 1)})
	draw()
	if w.value != 31 {
		t.Fatalf("turned towards the user: %d", w.value)
	}
	// A press on the one above brings it to the middle.
	above := wheelRowMiddle(gtx, -1)
	router.Queue(pointer.Event{Kind: pointer.Press, Source: pointer.Mouse, Buttons: pointer.ButtonPrimary, Position: above},
		pointer.Event{Kind: pointer.Release, Source: pointer.Mouse, Position: above})
	draw()
	if w.value != 30 {
		t.Fatalf("pressed above: %d", w.value)
	}
	// Dragged up by two rows, the second below comes to the middle.
	row := float32(gtx.Dp(wheelRow))
	router.Queue(pointer.Event{Kind: pointer.Press, Source: pointer.Mouse, Buttons: pointer.ButtonPrimary, Position: middle})
	draw()
	router.Queue(pointer.Event{Kind: pointer.Move, Source: pointer.Mouse, Buttons: pointer.ButtonPrimary, Position: middle.Sub(f32.Pt(0, 2*row))})
	draw()
	router.Queue(pointer.Event{Kind: pointer.Release, Source: pointer.Mouse, Position: middle.Sub(f32.Pt(0, 2*row))})
	draw()
	if w.value != 32 || w.offset != 0 {
		t.Fatalf("dragged up two rows: %d, offset %v", w.value, w.offset)
	}
}

// The windows over the Schedule page: Done writes what was picked into the field, and Set by hand
// waits for something in its field.
func TestTheSchedulesWindowsFillTheirFields(t *testing.T) {
	a := sceneApp(t, PageSchedule)
	// Without the next 24 hours, so that the clock module's card is in sight with the form open.
	a.schedule.timeline = nil
	a.schedule.openForm(newAction)
	frame := func() {
		var ops op.Ops
		var router input.Router
		a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight))
	}
	press := func(click func()) {
		click()
		frame()
		frame()
	}
	frame()
	press(a.schedule.form.timeOpen.Click)
	if a.schedule.popup != popupTime {
		t.Fatalf("the time window is %v", a.schedule.popup)
	}
	a.schedule.form.clock.hours.step(2)
	press(a.schedule.form.clock.done.Click)
	if a.schedule.popup != popupNone || a.schedule.form.at.Text() != "20:00" {
		t.Fatalf("window %v, time %q", a.schedule.popup, a.schedule.form.at.Text())
	}
	a.schedule.rtcText.SetText("")
	frame()
	press(a.schedule.rtcHand.Click)
	if strings.Contains(a.status, "must be") {
		t.Fatal("Set by hand acted on an empty field")
	}
	press(a.schedule.rtcPick.Click)
	if a.schedule.popup != popupClock {
		t.Fatalf("the clock window is %v", a.schedule.popup)
	}
	press(a.schedule.clockPick.done.Click)
	if _, ok := parseClockTyped(a.schedule.rtcText.Text()); !ok || a.schedule.popup != popupNone {
		t.Fatalf("the field holds %q", a.schedule.rtcText.Text())
	}
	// An effect is chosen from its group, and the window closes.
	a.schedule.form.useEffect.Value = true
	frame()
	press(a.schedule.form.effectOpen.Click)
	if a.schedule.popup != popupEffect {
		t.Fatalf("the effect window is %v", a.schedule.popup)
	}
	chooser := &a.schedule.form.chooser
	groups := GroupEffects(chooser.effects)
	if len(groups) == 0 {
		t.Fatal("no effects to choose from")
	}
	first := groups[0]
	press(clickOf(chooser.groups, first.Name).Click)
	press(clickOf(chooser.picks, first.Effects[0].ID).Click)
	if a.schedule.form.effect != first.Effects[0].ID || a.schedule.popup != popupNone {
		t.Fatalf("effect %q, window %v", a.schedule.form.effect, a.schedule.popup)
	}
}

// The day is held to its month before the wheels are drawn, in the same frame as the month's turn.
func TestTheDayIsHeldToItsMonthBeforeItIsDrawn(t *testing.T) {
	a := sceneApp(t, PageSchedule)
	var c clockPopup
	c.open(time.Date(2027, time.January, 31, 10, 0, 0, 0, time.UTC))
	c.month.step(1)
	var ops op.Ops
	var router input.Router
	gtx := sceneContext(&ops, &router, sceneWidth, sceneHeight)
	a.source = gtx.Source
	drawn := -1
	a.wheels(gtx, func() {
		c.fitDay()
		drawn = c.day.value
	}, wheelPart{wheel: &c.month}, wheelPart{wheel: &c.day})
	if drawn != 28 || c.day.high != 28 {
		t.Fatalf("drawn %d of %d", drawn, c.day.high)
	}
}

// A time typed out of range starts the window at the evening, not at a clamped part of it; and a
// window over the page goes with the journal.
func TestATimeOutOfRangeStartsTheWheelsAtTheEvening(t *testing.T) {
	a := sceneApp(t, PageSchedule)
	a.schedule.openForm(newAction)
	a.schedule.form.at.SetText("-5:30")
	var ops op.Ops
	var router input.Router
	a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight))
	a.schedule.form.timeOpen.Click()
	a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight))
	if a.schedule.form.clock.hours.value != 18 || a.schedule.form.clock.minutes.value != 0 {
		t.Fatalf("started at %d:%d", a.schedule.form.clock.hours.value, a.schedule.form.clock.minutes.value)
	}
	if !a.schedulePopupShown() {
		t.Fatal("the window is not shown")
	}
	a.journalOpen = true
	if a.schedulePopupShown() {
		t.Fatal("the window is shown over the journal")
	}
}
