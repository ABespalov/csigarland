// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The form of a scheduled action: its time, its days and its steps.
// Contributors: None

package ui

import (
	"fmt"
	"strconv"
	"strings"

	"gioui.org/layout"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
)

const (
	defaultActionBrightness = 50
	maxBrightness           = 100
)

var (
	daySegments   = segmentsOf(dayNames, func(name string) string { return name })
	speedSegments = segmentsOf(ScheduleSpeeds[:], SpeedText)
)

func segmentsOf[T any](values []T, words func(T) string) []segment {
	options := make([]segment, len(values))
	for i, value := range values {
		options[i] = segment{key: words(value), words: words(value)}
	}
	return options
}

// actionForm is what the form holds while it is open.
type actionForm struct {
	at          widget.Editor
	timeOpen    widget.Clickable
	days        [7]bool // Sunday first, as dayNames.
	dayChoice   choice
	kind        choice
	turnOn      widget.Bool
	rise        widget.Bool
	useBright   widget.Bool
	brightness  int
	brightField numberField
	useSpeed    widget.Bool
	speed       choice
	useEffect   widget.Bool
	usePlaylist widget.Bool
	effect      string
	effectOpen  widget.Clickable
	saveForm    widget.Clickable
	cancel      widget.Clickable
	clock       timePopup
	chooser     effectChooser
}

func (p *schedulePage) openForm(index int) {
	p.editing = index
	action := api.Action{Time: "18:00", Days: allDays, Enabled: true, Power: api.StepOn}
	if index >= 0 {
		action = p.schedule.Entries[index]
	}
	f := &p.form
	p.popup = popupNone
	f.at.SingleLine = true
	f.at.SetText(action.Time)
	for bit := range f.days {
		f.days[bit] = action.Days&(1<<bit) != 0
	}
	f.kind.Value = "steps"
	if action.Power == api.StepOff {
		f.kind.Value = "off"
	}
	f.turnOn.Value = action.Power == api.StepOn || action.Power == api.StepRise
	f.rise.Value = action.Power == api.StepRise
	f.useBright.Value = action.Brightness != 0
	f.brightness = action.Brightness
	if f.brightness == 0 {
		f.brightness = defaultActionBrightness
	}
	f.useSpeed.Value = action.Speed != 0
	speed := action.Speed
	if speed == 0 {
		speed = 1
	}
	f.speed.Value = SpeedText(speed)
	f.useEffect.Value = action.Effect != ""
	f.usePlaylist.Value = action.Playlist
	f.effect = action.Effect
}

func (p *schedulePage) readForm() (api.Action, error) {
	f := &p.form
	enabled := true
	if p.editing >= 0 {
		enabled = p.schedule.Entries[p.editing].Enabled
	}
	days := 0
	for bit, on := range f.days {
		if on {
			days |= 1 << bit
		}
	}
	speed := 0.0
	for _, value := range ScheduleSpeeds {
		if SpeedText(value) == f.speed.Value {
			speed = value
		}
	}
	return BuildAction(ActionForm{
		Enabled: enabled, Time: strings.TrimSpace(f.at.Text()), Days: days,
		TurnOff: f.kind.Value == "off", TurnOn: f.turnOn.Value, Rise: f.rise.Value,
		UseBrightness: f.useBright.Value, Brightness: strconv.Itoa(f.brightness),
		UseSpeed: f.useSpeed.Value, Speed: speed,
		UseEffect: f.useEffect.Value, Effect: f.effect, UsePlaylist: f.usePlaylist.Value,
	})
}

func (p *schedulePage) formCard(a *App) layout.Widget {
	f := &p.form
	return func(gtx C) D {
		// What is sent is the form as it stood, and its answer closes the form, so nothing changes until then.
		if p.saving {
			gtx = gtx.Disabled()
		}
		if f.cancel.Clicked(gtx) && !p.saving {
			p.editing = noAction
		}
		if f.saveForm.Clicked(gtx) && !p.saving {
			action, err := p.readForm()
			if err != nil {
				a.reportError(err)
			} else {
				changed := append([]api.Action(nil), p.schedule.Entries...)
				if p.editing >= 0 {
					changed[p.editing] = action
				} else {
					changed = append(changed, action)
				}
				p.save(a, changed)
			}
		}
		if f.timeOpen.Clicked(gtx) {
			// From what is typed, if it is a time, or else the evening.
			hour, minute := 18, 0
			if _, err := fmt.Sscanf(strings.TrimSpace(f.at.Text()), "%d:%d", &hour, &minute); err != nil || hour < 0 || hour > 23 || minute < 0 || minute > 59 {
				hour, minute = 18, 0
			}
			f.clock.open(hour, minute)
			p.popup = popupTime
		}
		rows := []layout.Widget{
			func(gtx C) D {
				return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
					layout.Rigid(a.label(sizeSmall, colorMuted, "Time").Layout),
					layout.Rigid(layout.Spacer{Width: gapLarge}.Layout),
					layout.Rigid(func(gtx C) D { return a.field(gtx, &f.at, "18:00", 90) }),
					layout.Rigid(layout.Spacer{Width: gap}.Layout),
					layout.Rigid(func(gtx C) D { return a.iconButton(gtx, &f.timeOpen, buttonOther, "clock", "Pick the time", true) }),
				)
			},
			func(gtx C) D { return a.toggles(gtx, &f.dayChoice, daySegments, f.days[:]) },
			func(gtx C) D { return a.segments(gtx, &f.kind, actionKinds) },
		}
		if f.kind.Value != "off" {
			rows = append(rows, p.stepRows(a)...)
		}
		rows = append(rows, func(gtx C) D {
			return row(gtx,
				func(gtx C) D {
					return a.labelButton(gtx, &f.saveForm, buttonMain, "save", "Save action", "", !p.saving)
				},
				func(gtx C) D { return a.labelButton(gtx, &f.cancel, buttonOther, "close", "Cancel", "", true) },
			)
		})
		return a.card(gtx, "Action", rows...)
	}
}

// A step's box, and what it is set to beside it once it is ticked.
func (a *App) stepLine(gtx C, box *widget.Bool, words string, value layout.Widget) D {
	children := []layout.FlexChild{layout.Rigid(func(gtx C) D { return a.checkbox(gtx, box, words) })}
	if box.Value && value != nil {
		children = append(children, layout.Rigid(layout.Spacer{Width: gapLarge}.Layout), layout.Rigid(value))
	}
	return layout.Flex{Alignment: layout.Middle}.Layout(gtx, children...)
}

func (p *schedulePage) stepRows(a *App) []layout.Widget {
	f := &p.form
	return []layout.Widget{
		func(gtx C) D {
			return a.stepLine(gtx, &f.turnOn, "Turn on", func(gtx C) D { return a.checkbox(gtx, &f.rise, "Rising from dark over a minute") })
		},
		func(gtx C) D {
			return a.stepLine(gtx, &f.useBright, "Brightness, %", func(gtx C) D {
				return a.numberStepper(gtx, &f.brightField, f.brightness, 1, maxBrightness, func(value int) { f.brightness = value })
			})
		},
		func(gtx C) D {
			return a.stepLine(gtx, &f.useSpeed, "Speed", func(gtx C) D { return a.segments(gtx, &f.speed, speedSegments) })
		},
		func(gtx C) D {
			if f.effectOpen.Clicked(gtx) {
				f.chooser.open(p.effects, f.effect)
				p.popup = popupEffect
			}
			return a.stepLine(gtx, &f.useEffect, "Start effect (also turns on)", func(gtx C) D {
				words := "Choose an effect"
				if f.effect != "" {
					words = p.effectName(api.Action{Effect: f.effect})
				}
				return a.labelButton(gtx, &f.effectOpen, buttonOther, "list", words, "Choose the effect", true)
			})
		},
		func(gtx C) D {
			// An effect or the playlist: one start at a time, the last ticked.
			if f.usePlaylist.Update(gtx) && f.usePlaylist.Value {
				f.useEffect.Value = false
			}
			if f.useEffect.Value && f.usePlaylist.Value {
				f.usePlaylist.Value = false
			}
			return a.checkbox(gtx, &f.usePlaylist, "Start the playlist (also turns on)")
		},
	}
}
