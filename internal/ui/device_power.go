// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Device page's power card: the supply, an LED's draw and the margin, and the limit they give.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"strconv"

	"gioui.org/layout"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/engine"
)

// powerForm is the card, shown only for a garland that has the settings: one of 1.1.1 or before lacks them.
type powerForm struct {
	held   bool
	last   api.PowerBudget
	supply widget.Editor
	white  widget.Editor
	idle   widget.Editor
	margin widget.Editor
	save   widget.Clickable
	saving bool
}

func (f *powerForm) init() {
	f.supply = newField(true)
	f.white = newField(true)
	f.idle = newField(true)
	f.margin = newField(true)
}

// readPowerBudget reads the settings, and whether the garland has them at all: 404 is a release before them.
func readPowerBudget(ctx context.Context, client *api.Client) (api.PowerBudget, bool, error) {
	budget, err := client.PowerBudget(ctx)
	var failure *api.Error
	if errors.As(err, &failure) && failure.Status == http.StatusNotFound {
		return api.PowerBudget{}, false, nil
	}
	if err != nil {
		return api.PowerBudget{}, false, fmt.Errorf("read the power settings: %w", err)
	}
	return budget, true, nil
}

// take fills the form as the garland has it, and has Tune go by the same.
func (f *powerForm) take(budget api.PowerBudget, held bool) {
	f.takeTotals(budget, held)
	if !held {
		return
	}
	f.supply.SetText(strconv.Itoa(budget.SupplyMilliamps))
	f.white.SetText(strconv.Itoa(budget.WhiteMilliamps))
	f.idle.SetText(strconv.Itoa(budget.IdleMilliamps))
	f.margin.SetText(strconv.Itoa(budget.MarginPercent))
	_ = engine.SetPower(enginePower(budget))
}

// takeTotals takes what the settings come to, leaving what is typed: after a change of the LED count.
func (f *powerForm) takeTotals(budget api.PowerBudget, held bool) {
	f.held = held
	if held {
		f.last = budget
	}
}

// PowerText says what the settings come to, as the portal does.
func PowerText(budget api.PowerBudget) string {
	held := "under the limit"
	if budget.PeakMilliamps > budget.LimitMilliamps {
		held = "so it is dimmed to the limit"
	}
	return fmt.Sprintf("Limit %d mA: what the supply gives beyond %d mA for the board, the screen and Wi-Fi, less the margin. %d LEDs at full white draw %d mA, %s. Full white stays undimmed on up to %d LEDs.",
		budget.LimitMilliamps, budget.BoardMilliamps, budget.Leds, budget.PeakMilliamps, held, budget.UndimmedLeds)
}

func (f *powerForm) card(a *App) layout.Widget {
	return func(gtx C) D {
		if f.saving {
			gtx = gtx.Disabled()
		}
		if f.save.Clicked(gtx) {
			f.store(a)
		}
		return a.card(gtx, "Power",
			a.hint(PowerText(f.last)),
			a.hint("Power supply, mA"),
			func(gtx C) D { return a.field(gtx, &f.supply, "2000", 90) },
			a.hint("One LED at full white, mA, without its idle draw"),
			func(gtx C) D { return a.field(gtx, &f.white, "40", 90) },
			a.hint("One LED dark, mA"),
			func(gtx C) D { return a.field(gtx, &f.idle, "1", 90) },
			a.hint("Margin, % of the supply beyond the board"),
			func(gtx C) D { return a.field(gtx, &f.margin, "20", 90) },
			func(gtx C) D { return a.labelButton(gtx, &f.save, buttonMain, "save", "Save", "", true) },
		)
	}
}

func (f *powerForm) read() (api.PowerBudget, error) {
	var wanted api.PowerBudget
	var err error
	if wanted.SupplyMilliamps, err = number(&f.supply, "the power supply"); err != nil {
		return wanted, err
	}
	if wanted.WhiteMilliamps, err = number(&f.white, "an LED at full white"); err != nil {
		return wanted, err
	}
	if wanted.IdleMilliamps, err = number(&f.idle, "an LED dark"); err != nil {
		return wanted, err
	}
	wanted.MarginPercent, err = number(&f.margin, "the margin")
	return wanted, err
}

func (f *powerForm) store(a *App) {
	wanted, err := f.read()
	if err != nil {
		a.reportError(err)
		return
	}
	// The form holds still until the answer, which would otherwise replace what was typed since.
	f.saving = true
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		budget, err := client.SetPowerBudget(ctx, wanted)
		if err != nil {
			return func() { f.saving = false }, fmt.Errorf("could not save: %w", err)
		}
		return func() {
			f.saving = false
			f.take(budget, true)
			a.report("Saved.")
		}, nil
	})
}
