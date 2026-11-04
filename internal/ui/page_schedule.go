// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Schedule page: the next 24 hours, the actions, the time zone and the clock module.
// Contributors: None

package ui

import (
	"context"
	"fmt"
	"strings"
	"time"

	"gioui.org/layout"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
)

const (
	noAction     = -1
	newAction    = -2
	ntpLooks     = 5
	ntpLookEvery = 2 * time.Second
	ntpSource    = "ntp"
)

var actionKinds = []segment{{"steps", "Do these steps"}, {"off", "Turn off (nothing else can go with it)"}}

type actionRow struct {
	enabled widget.Bool
	edit    widget.Clickable
	remove  confirmClick
}

type schedulePage struct {
	schedule       api.Schedule
	loaded         bool
	clock          api.Clock
	names          map[string]string
	effects        []api.Effect
	saving         bool
	rowsOf         []*actionRow
	add            widget.Clickable
	editing        int
	form           actionForm
	zoneOpen       widget.Clickable
	showZones      bool
	zones          [len(Zones)]widget.Clickable
	rtcHere        widget.Clickable
	rtcNet         widget.Clickable
	rtcHand        widget.Clickable
	rtcText        widget.Editor
	rtcPick        widget.Clickable
	clockPick      clockPopup
	popup          schedulePopup
	timeline       *api.Timeline // The next 24 hours, nil until read or from a garland with no such route.
	timelineAt     time.Time     // When the day shown was read, so its time can be carried on.
	timelineTried  time.Time     // When it was last asked for, read or not: see keepTimeline.
	timelineAsked  bool          // One reading at a time.
	timelineGen    int           // Moves with a change that moves the day: see loadTimeline.
	timelineNone   bool          // The garland has no such route, as 1.0.0 has not.
	timelineFailed bool          // The last reading failed, and is tried again soon.
}

func (p *schedulePage) init() {
	p.editing = noAction
	p.names = map[string]string{}
	p.form.chooser.filter = newField(true)
	p.rtcText = newField(true)
}

func (p *schedulePage) load(a *App) {
	p.loadTimeline(a)
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		schedule, err := client.Schedule(ctx)
		if err != nil {
			return nil, err
		}
		clock, err := client.Clock(ctx)
		if err != nil {
			return nil, err
		}
		effects, err := client.Effects(ctx)
		if err != nil {
			return nil, err
		}
		return func() {
			p.take(schedule)
			p.clock = clock
			p.effects = effects
			for _, effect := range effects {
				p.names[effect.ID] = effect.Name
			}
		}, nil
	})
}

// A form holds its action's place in the list, which says nothing of a list that has changed since.
// A new action has no place to lose. Nor does a row's button, so a Delete pressed once is not left
// waiting to take whatever action comes to that place.
func (p *schedulePage) take(schedule api.Schedule) {
	if p.loaded && schedule.Revision != p.schedule.Revision {
		if p.editing >= 0 {
			p.editing = noAction
		}
		p.rowsOf = nil
	}
	p.schedule = schedule
	p.loaded = true
	for len(p.rowsOf) < len(schedule.Entries) {
		p.rowsOf = append(p.rowsOf, &actionRow{})
	}
}

func (p *schedulePage) rows(a *App) []layout.Widget {
	p.keepTimeline(a)
	// The next 24 hours say what is due next and more; a garland of 1.0.0 has only the next action.
	rows := []layout.Widget{p.summaryCard(a)}
	if p.timeline != nil {
		rows = []layout.Widget{p.timelineCard(a)}
	}
	rows = append(rows, p.actionsCard(a))
	if p.editing != noAction {
		rows = append(rows, p.formCard(a))
	}
	rows = append(rows, p.zoneCard(a))
	if p.clock.Module.Present {
		rows = append(rows, p.moduleCard(a))
	}
	return rows
}

func (p *schedulePage) summaryCard(a *App) layout.Widget {
	return func(gtx C) D {
		return a.card(gtx, "", a.body("Next: "+NextText(p.schedule.Next)), a.hint(p.clockText()))
	}
}

func (p *schedulePage) clockText() string {
	if p.clock.Synced && p.clock.Now == "" {
		return "The garland's clock is set."
	}
	if p.clock.Synced {
		return "Garland time: " + p.clock.Now + "."
	}
	return "The clock is not set: it syncs over the internet once the garland joins your home Wi-Fi. Actions wait until then."
}

func (p *schedulePage) effectName(action api.Action) string {
	if name, known := p.names[action.Effect]; known {
		return name
	}
	return action.Effect
}

func (p *schedulePage) actionsCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.add.Clicked(gtx) && !p.saving {
			p.openForm(newAction)
		}
		rows := []layout.Widget{}
		order := sortedByTime(p.schedule.Entries)
		for _, index := range order {
			rows = append(rows, p.actionWidget(a, index))
		}
		if len(p.schedule.Entries) == 0 && p.loaded {
			rows = append(rows, a.hint("No actions yet."))
		}
		rows = append(rows, func(gtx C) D {
			enabled := p.loaded && !p.saving && p.editing == noAction && len(p.schedule.Entries) < p.schedule.Max
			return a.labelButton(gtx, &p.add, buttonMain, "add", "Add action", "", enabled)
		})
		return a.card(gtx, fmt.Sprintf("Actions (%d of %d)", len(p.schedule.Entries), p.schedule.Max), rows...)
	}
}

func sortedByTime(entries []api.Action) []int {
	order := make([]int, len(entries))
	for i := range order {
		order[i] = i
	}
	for i := 1; i < len(order); i++ {
		for j := i; j > 0 && entries[order[j]].Time < entries[order[j-1]].Time; j-- {
			order[j], order[j-1] = order[j-1], order[j]
		}
	}
	return order
}

func (p *schedulePage) actionWidget(a *App, index int) layout.Widget {
	return func(gtx C) D {
		if index >= len(p.schedule.Entries) {
			return D{}
		}
		action := p.schedule.Entries[index]
		widgets := p.rowsOf[index]
		widgets.enabled.Value = action.Enabled
		if widgets.enabled.Update(gtx) && !p.saving && p.editing == noAction {
			changed := append([]api.Action(nil), p.schedule.Entries...)
			changed[index].Enabled = widgets.enabled.Value
			p.save(a, changed)
		}
		if widgets.edit.Clicked(gtx) && !p.saving {
			p.openForm(index)
		}
		if widgets.remove.confirmed(gtx) && !p.saving {
			p.save(a, append(append([]api.Action(nil), p.schedule.Entries[:index]...), p.schedule.Entries[index+1:]...))
		}
		enabled := !p.saving && p.editing == noAction
		return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
			layout.Rigid(func(gtx C) D { return a.checkbox(gtx, &widgets.enabled, "") }),
			layout.Rigid(layout.Spacer{Width: gap}.Layout),
			layout.Flexed(1, func(gtx C) D {
				return a.column(gtx,
					a.body(fmt.Sprintf("%s, %s", action.Time, DaysText(action.Days))),
					a.hint(StepsText(action.Power, action.Brightness, action.Speed, p.effectName(action), action.Playlist)),
				)
			}),
			layout.Rigid(func(gtx C) D { return a.iconButton(gtx, &widgets.edit, buttonOther, "edit", "Edit", enabled) }),
			layout.Rigid(layout.Spacer{Width: gapSmall}.Layout),
			layout.Rigid(func(gtx C) D { return a.confirmButton(gtx, &widgets.remove, buttonLosing, "trash", "Delete", enabled) }),
		)
	}
}

// Sent whole, with the revision it was worked out from. If the list has changed since, it is read
// again rather than overwritten.
func (p *schedulePage) save(a *App, entries []api.Action) {
	if a.client == nil {
		return
	}
	revision := p.schedule.Revision
	p.saving = true
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		answer, err := client.SetSchedule(ctx, entries, revision)
		if err == nil {
			return func() {
				p.saving = false
				p.take(answer)
				p.editing = noAction
				a.report("Saved.")
				p.loadTimeline(a)
			}, nil
		}
		var reread *api.Schedule
		if api.IsConflict(err) {
			if read, readErr := client.Schedule(ctx); readErr == nil {
				reread = &read
			}
		}
		return func() {
			p.saving = false
			if reread != nil {
				p.take(*reread)
			}
		}, fmt.Errorf("could not save: %w", err)
	})
}

func (p *schedulePage) zoneCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.zoneOpen.Clicked(gtx) {
			p.showZones = !p.showZones
		}
		rows := []layout.Widget{
			func(gtx C) D {
				return a.button(gtx, &p.zoneOpen, buttonOther, ZoneLabel(p.clock.Zone), "Choose the time zone", true)
			},
		}
		if p.showZones {
			rows = append(rows, func(gtx C) D {
				items := make([]layout.Widget, len(Zones))
				for i := range Zones {
					items[i] = func(gtx C) D {
						if p.zones[i].Clicked(gtx) {
							p.setZone(a, Zones[i][1])
						}
						return a.button(gtx, &p.zones[i], buttonOther, ZoneLabel(Zones[i][1]), "Use this time zone", true)
					}
				}
				return row(gtx, items...)
			})
		}
		return a.card(gtx, "Time zone", rows...)
	}
}

func (p *schedulePage) setZone(a *App, zone string) {
	p.showZones = false
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		clock, err := client.SetZone(ctx, zone)
		if err != nil {
			return nil, err
		}
		schedule, err := client.Schedule(ctx)
		if err != nil {
			return nil, err
		}
		return func() {
			p.clock = clock
			p.take(schedule)
			a.report("Time zone saved.")
			p.loadTimeline(a)
		}, nil
	})
}

func (p *schedulePage) moduleCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.rtcHere.Clicked(gtx) {
			p.setModule(a, func(ctx context.Context, client *api.Client) (api.Clock, error) {
				return client.SetModuleFromClock(ctx, time.Now())
			})
		}
		if p.rtcNet.Clicked(gtx) {
			p.setModuleFromInternet(a)
		}
		// Picking starts at what is typed, if it is a time, or else the module's, the garland's or this
		// computer's.
		if p.rtcPick.Clicked(gtx) {
			at, typed := parseClockTyped(p.rtcText.Text())
			if !typed {
				at = startTime(p.clock, gtx.Now)
			}
			p.clockPick.open(at)
			p.popup = popupClock
		}
		typed := strings.TrimSpace(p.rtcText.Text()) != ""
		if p.rtcHand.Clicked(gtx) && typed {
			if at, ok := parseClockTyped(p.rtcText.Text()); !ok {
				a.reportError(fmt.Errorf("the time must be as 2026-12-25 18:00, in %d to %d", pickerFirstYear, pickerLastYear))
			} else {
				local := at.Format(pickerLayout)
				p.setModule(a, func(ctx context.Context, client *api.Client) (api.Clock, error) {
					return client.SetModuleByHand(ctx, local)
				})
			}
		}
		state := "The module lost its time, perhaps with its battery. It is set from the internet as soon as there is a connection, or here."
		if p.clock.Module.Trusted {
			state = fmt.Sprintf("Module time: %s. It follows the internet time once an hour.", p.clock.Module.Now)
		}
		return a.card(gtx, "Clock module",
			a.hint(state),
			func(gtx C) D {
				return row(gtx,
					func(gtx C) D {
						return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
							layout.Rigid(func(gtx C) D { return a.field(gtx, &p.rtcText, "2026-12-25 18:00", 170) }),
							layout.Rigid(layout.Spacer{Width: gap}.Layout),
							layout.Rigid(func(gtx C) D {
								return a.iconButton(gtx, &p.rtcPick, buttonOther, "clock", "Pick the date and time", true)
							}),
						)
					},
					func(gtx C) D {
						return a.labelButton(gtx, &p.rtcHand, buttonOther, "edit", "Set by hand", "Set the module to this time, in the garland's time zone", typed)
					},
				)
			},
			func(gtx C) D {
				return row(gtx,
					func(gtx C) D {
						return a.labelButton(gtx, &p.rtcHere, buttonOther, "computer", "Set from this computer", "", true)
					},
					func(gtx C) D {
						return a.labelButton(gtx, &p.rtcNet, buttonOther, "globe", "Set from internet", "", true)
					},
				)
			},
		)
	}
}

func (p *schedulePage) setModule(a *App, set func(context.Context, *api.Client) (api.Clock, error)) {
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		clock, err := set(ctx, client)
		if err != nil {
			return nil, fmt.Errorf("could not set the clock: %w", err)
		}
		// A clock that was not set may be what the schedule's next action was waiting for.
		schedule, err := client.Schedule(ctx)
		if err != nil {
			return nil, err
		}
		return func() {
			p.clock = clock
			p.take(schedule)
			a.report("Clock module set.")
			p.loadTimeline(a)
		}, nil
	})
}

// setModuleFromInternet takes its turn among the commands, and only starts the garland asking.
func (p *schedulePage) setModuleFromInternet(a *App) {
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		asked, err := client.SetModuleFromInternet(ctx)
		if err != nil {
			return nil, fmt.Errorf("could not set the clock: %w", err)
		}
		return func() {
			p.clock = asked
			p.watchInternetClock(a, asked)
		}, nil
	})
}

// Long enough not to hold up what comes after it, so looked at apart from the commands. A list saved
// meanwhile is newer than what this reads, so only the next action is taken, and only of the same list.
func (p *schedulePage) watchInternetClock(a *App, asked api.Clock) {
	a.aside(func(ctx context.Context, client *api.Client) (func(), error) {
		clock := asked
		for range ntpLooks {
			if internetClockArrived(asked, clock) {
				break
			}
			select {
			case <-ctx.Done():
				return nil, ctx.Err()
			case <-time.After(ntpLookEvery):
			}
			var err error
			if clock, err = client.Clock(ctx); err != nil {
				return nil, err
			}
		}
		schedule, err := client.Schedule(ctx)
		if err != nil {
			return nil, err
		}
		return func() {
			p.clock = clock
			if schedule.Revision == p.schedule.Revision {
				p.schedule.Next = schedule.Next
			}
			a.report(InternetClockText(asked, clock))
			p.loadTimeline(a)
		}, nil
	})
}

func internetClockArrived(asked, now api.Clock) bool {
	return asked.Module.Source != ntpSource && now.Synced && now.Module.Trusted && now.Module.Source == ntpSource
}

// InternetClockText says what can be said. The source is that of the time the module has, so one that
// came from the internet before says nothing of this asking, and the answer has no more to tell.
func InternetClockText(asked, now api.Clock) string {
	switch {
	case internetClockArrived(asked, now):
		return "Clock module set from the internet."
	case asked.Module.Source == ntpSource:
		return "Asked the garland to set the clock from the internet. Its time came from there before, so whether this try reached it does not show."
	default:
		return "Asked the garland to set the clock from the internet, and it has not yet."
	}
}
