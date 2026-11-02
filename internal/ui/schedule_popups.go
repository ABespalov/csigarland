// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Schedule page's windows: a time, a date and time, and an effect by its group.
// Contributors: None

package ui

import (
	"fmt"
	"image"
	"strings"
	"time"

	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"

	"github.com/ABespalov/csigarland/internal/api"
)

// schedulePopup is the window open over the Schedule page, if any.
type schedulePopup int

const (
	popupNone schedulePopup = iota
	popupTime
	popupClock
	popupEffect
)

const (
	chooserWide = unit.Dp(640)
	chooserHigh = unit.Dp(560)
	// At the least, for the title and the buttons.
	pickWindowWide = unit.Dp(300)
	// The module's time as it is typed, and as the window writes it.
	clockTyped = "2006-01-02 15:04"
)

// timePopup picks an hour and a minute.
type timePopup struct {
	hours, minutes wheel
	done, cancel   widget.Clickable
}

func (t *timePopup) open(hour, minute int) {
	t.hours.set(hour, 0, 23, true, 2)
	t.minutes.set(minute, 0, 59, true, 2)
}

// clockPopup picks a date and a time, the day kept to its month.
type clockPopup struct {
	year, month, day, hour, minute wheel
	done, cancel                   widget.Clickable
}

func (c *clockPopup) open(at time.Time) {
	c.year.set(at.Year(), pickerFirstYear, pickerLastYear, false, 4)
	c.month.set(int(at.Month()), 1, 12, true, 2)
	c.day.set(at.Day(), 1, 31, true, 2)
	c.hour.set(at.Hour(), 0, 23, true, 2)
	c.minute.set(at.Minute(), 0, 59, true, 2)
	c.fitDay()
}

// A month has its own number of days, and a day past them takes the month's last.
func (c *clockPopup) fitDay() {
	c.day.high = time.Date(c.year.value, time.Month(c.month.value)+1, 0, 0, 0, 0, 0, time.UTC).Day()
	c.day.value = min(c.day.value, c.day.high)
}

func (c *clockPopup) value() time.Time {
	return time.Date(c.year.value, time.Month(c.month.value), c.day.value, c.hour.value, c.minute.value, 0, 0, time.UTC)
}

// parseClockTyped reads a date and time as typed, within the firmware's years.
func parseClockTyped(text string) (time.Time, bool) {
	text = strings.TrimSpace(text)
	for _, layout := range []string{clockTyped, pickerLayout} {
		if at, err := time.Parse(layout, text); err == nil && at.Year() >= pickerFirstYear && at.Year() <= pickerLastYear {
			return at, true
		}
	}
	return time.Time{}, false
}

// effectChooser picks an effect from its group, as the Effects page lists them.
type effectChooser struct {
	filter  widget.Editor
	list    widget.List
	effects []api.Effect
	chosen  string
	opened  map[string]bool
	groups  map[string]*widget.Clickable
	picks   map[string]*widget.Clickable
	cancel  widget.Clickable
}

// open starts with the group of the effect chosen open, and the others closed.
func (c *effectChooser) open(effects []api.Effect, chosen string) {
	c.effects, c.chosen = effects, chosen
	c.filter.SingleLine = true
	c.filter.SetText("")
	c.list.Axis = layout.Vertical
	c.list.Position = layout.Position{}
	c.opened = map[string]bool{}
	for _, group := range GroupEffects(effects) {
		for _, effect := range group.Effects {
			if effect.ID == chosen {
				c.opened[group.Name] = true
			}
		}
	}
	if c.groups == nil {
		c.groups, c.picks = map[string]*widget.Clickable{}, map[string]*widget.Clickable{}
	}
}

func clickOf(clicks map[string]*widget.Clickable, key string) *widget.Clickable {
	if clicks[key] == nil {
		clicks[key] = new(widget.Clickable)
	}
	return clicks[key]
}

// popupWindow is the open window's body, or nil. A window of the form closes with the form.
func (p *schedulePage) popupWindow(a *App) layout.Widget {
	if p.popup != popupClock && p.editing == noAction {
		p.popup = popupNone
	}
	switch p.popup {
	case popupTime:
		return p.timeWindow(a)
	case popupClock:
		return p.clockWindow(a)
	case popupEffect:
		return p.effectWindow(a)
	}
	return nil
}

// A window's title, then what it picks with, then Cancel and Done, as wide as what it picks with.
func (a *App) pickWindow(gtx C, title string, cancel, done *widget.Clickable, body layout.Widget) D {
	free := gtx
	free.Constraints.Min = image.Point{}
	macro := op.Record(gtx.Ops)
	picked := body(free)
	drawn := macro.Stop()
	gtx.Constraints.Max.X = min(gtx.Constraints.Max.X, max(picked.Size.X, gtx.Dp(pickWindowWide)))
	gtx.Constraints.Min.X = gtx.Constraints.Max.X
	return a.column(gtx,
		a.heading(title),
		func(gtx C) D {
			return layout.Inset{Top: gap, Bottom: gap}.Layout(gtx, func(gtx C) D {
				// In the middle of the window.
				shift := op.Offset(image.Pt((gtx.Constraints.Max.X-picked.Size.X)/2, 0)).Push(gtx.Ops)
				drawn.Add(gtx.Ops)
				shift.Pop()
				return D{Size: image.Pt(gtx.Constraints.Max.X, picked.Size.Y)}
			})
		},
		func(gtx C) D {
			return layout.Flex{}.Layout(gtx,
				layout.Flexed(1, func(gtx C) D { return D{Size: image.Pt(gtx.Constraints.Min.X, 0)} }),
				layout.Rigid(func(gtx C) D { return a.labelButton(gtx, cancel, buttonOther, "close", "Cancel", "", true) }),
				layout.Rigid(layout.Spacer{Width: gap}.Layout),
				layout.Rigid(func(gtx C) D { return a.labelButton(gtx, done, buttonMain, "check", "Done", "", true) }),
			)
		},
	)
}

func (p *schedulePage) timeWindow(a *App) layout.Widget {
	t := &p.form.clock
	return func(gtx C) D {
		if t.cancel.Clicked(gtx) {
			p.popup = popupNone
			return D{}
		}
		if t.done.Clicked(gtx) {
			p.form.at.SetText(fmt.Sprintf("%02d:%02d", t.hours.value, t.minutes.value))
			p.popup = popupNone
			return D{}
		}
		return a.pickWindow(gtx, "Time", &t.cancel, &t.done, func(gtx C) D {
			return a.wheels(gtx, nil, wheelPart{wheel: &t.hours}, wheelPart{words: ":"}, wheelPart{wheel: &t.minutes})
		})
	}
}

func (p *schedulePage) clockWindow(a *App) layout.Widget {
	c := &p.clockPick
	return func(gtx C) D {
		if c.cancel.Clicked(gtx) {
			p.popup = popupNone
			return D{}
		}
		if c.done.Clicked(gtx) {
			p.rtcText.SetText(c.value().Format(clockTyped))
			p.popup = popupNone
			return D{}
		}
		c.fitDay()
		return a.pickWindow(gtx, "Date and time, on the garland's clock", &c.cancel, &c.done, func(gtx C) D {
			return a.wheels(gtx, c.fitDay,
				wheelPart{wheel: &c.year}, wheelPart{words: "-"}, wheelPart{wheel: &c.month}, wheelPart{words: "-"},
				wheelPart{wheel: &c.day}, wheelPart{words: " "}, wheelPart{wheel: &c.hour}, wheelPart{words: ":"},
				wheelPart{wheel: &c.minute})
		})
	}
}

func (p *schedulePage) effectWindow(a *App) layout.Widget {
	c := &p.form.chooser
	return func(gtx C) D {
		if c.cancel.Clicked(gtx) {
			p.popup = popupNone
			return D{}
		}
		gtx.Constraints = layout.Exact(image.Pt(min(gtx.Constraints.Max.X, gtx.Dp(chooserWide)), min(gtx.Constraints.Max.Y, gtx.Dp(chooserHigh))))
		words := strings.ToLower(strings.TrimSpace(c.filter.Text()))
		var rows []layout.Widget
		for _, group := range GroupEffects(c.effects) {
			var found []api.Effect
			for _, effect := range group.Effects {
				if words == "" || strings.Contains(strings.ToLower(effect.Name), words) {
					found = append(found, effect)
				}
			}
			if len(found) == 0 {
				continue
			}
			toggle := clickOf(c.groups, group.Name)
			if toggle.Clicked(gtx) {
				c.opened[group.Name] = !c.opened[group.Name]
			}
			open := c.opened[group.Name] || words != ""
			mark := "+ "
			if open {
				mark = "- "
			}
			title := fmt.Sprintf("%s%s (%d)", mark, group.Name, len(found))
			rows = append(rows, func(gtx C) D {
				return toggle.Layout(gtx, func(gtx C) D {
					gtx.Constraints.Min.X = gtx.Constraints.Max.X
					return layout.Inset{Top: gapSmall, Bottom: gapSmall}.Layout(gtx, a.heading(title))
				})
			})
			if !open {
				continue
			}
			picks := make([]layout.Widget, len(found))
			for i, effect := range found {
				pick := clickOf(c.picks, effect.ID)
				if pick.Clicked(gtx) {
					p.form.effect = effect.ID
					p.popup = popupNone
					return D{}
				}
				kind := buttonOther
				if effect.ID == c.chosen {
					kind = buttonMain
				}
				picks[i] = func(gtx C) D { return a.button(gtx, pick, kind, effect.Name, "", true) }
			}
			rows = append(rows, func(gtx C) D { return row(gtx, picks...) })
		}
		if len(rows) == 0 {
			rows = append(rows, a.hint("No effect has these words in its name."))
		}
		return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
			layout.Rigid(func(gtx C) D {
				return a.modalTitle(gtx, "Effect", func(gtx C) D {
					return a.labelButton(gtx, &c.cancel, buttonOther, "close", "Cancel", "", true)
				})
			}),
			layout.Rigid(func(gtx C) D {
				return layout.Inset{Top: gap, Bottom: gap}.Layout(gtx, func(gtx C) D {
					return a.field(gtx, &c.filter, "Type to find an effect", 0)
				})
			}),
			layout.Flexed(1, func(gtx C) D {
				return material.List(a.theme, &c.list).Layout(gtx, len(rows), func(gtx C, i int) D {
					return layout.Inset{Bottom: gap, Right: gap}.Layout(gtx, rows[i])
				})
			}),
		)
	}
}
