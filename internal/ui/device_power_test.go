// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the Device page's power card: what it takes, says and reads back.
// Contributors: None

package ui

import (
	"strings"
	"testing"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/engine"
)

// The power card takes the garland's settings, says what they come to as the portal does, and reads
// back only numbers; a garland from before them shows no card.
func TestThePowerCardReadsWhatIsTyped(t *testing.T) {
	defaults := engine.DefaultPower()
	t.Cleanup(func() { _ = engine.SetPower(defaults) })
	var page devicePage
	page.init()
	page.power.take(api.PowerBudget{}, false)
	if page.power.held {
		t.Fatal("a garland without power settings has the card")
	}
	page.power.take(api.PowerBudget{SupplyMilliamps: 5000, WhiteMilliamps: 36, IdleMilliamps: 1, MarginPercent: 10, BoardMilliamps: 200, LimitMilliamps: 4300, Leds: 32, PeakMilliamps: 1184, UndimmedLeds: 116}, true)
	if !page.power.held || engine.LimitMilliamps() != 4300 {
		t.Fatalf("held %v, Tune's limit %d", page.power.held, engine.LimitMilliamps())
	}
	words := PowerText(page.power.last)
	if !strings.Contains(words, "Limit 4300 mA") || !strings.Contains(words, "draw 1184 mA, under the limit") || !strings.Contains(words, "up to 116 LEDs") {
		t.Fatalf("%q", words)
	}
	wanted, err := page.power.read()
	if err != nil || wanted.SupplyMilliamps != 5000 || wanted.WhiteMilliamps != 36 || wanted.IdleMilliamps != 1 || wanted.MarginPercent != 10 {
		t.Fatalf("%+v, %v", wanted, err)
	}
	page.power.margin.SetText("ten")
	if _, err := page.power.read(); err == nil {
		t.Fatal("a margin that is no number was taken")
	}
	if !strings.Contains(PowerText(api.PowerBudget{LimitMilliamps: 1400, Leds: 32, PeakMilliamps: 1792}), "dimmed to the limit") {
		t.Fatal("a chain past the limit is not said to be dimmed")
	}
}
