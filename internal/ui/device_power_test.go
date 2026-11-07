// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the Device page's power card: what it takes, says and reads back.
// Contributors: None

package ui

import (
	"errors"
	"net/http"
	"strings"
	"sync/atomic"
	"testing"
	"time"

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
	page.power.take(api.PowerBudget{SupplyMilliamps: 5000, WhiteMilliamps: 36, IdleMilliamps: 1, MarginPercent: 10, BoardMilliamps: 250, LimitMilliamps: 4275, Leds: 32, PeakMilliamps: 1184, UndimmedLeds: 115}, true)
	if !page.power.held || engine.LimitMilliamps() != 4275 {
		t.Fatalf("held %v, Tune's limit %d", page.power.held, engine.LimitMilliamps())
	}
	words := PowerText(page.power.last)
	if !strings.Contains(words, "Limit 4275 mA") || !strings.Contains(words, "draw 1184 mA, under the limit") || !strings.Contains(words, "up to 115 LEDs") {
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

// Tune's current is another garland's while the one connected to cannot have its settings read: none is
// shown then, and they are asked for again until they are.
func TestTuneShowsNoCurrentOfAnotherGarland(t *testing.T) {
	defaults := engine.DefaultPower()
	t.Cleanup(func() { _ = engine.SetPower(defaults) })
	a := sceneApp(t, PageEffects)
	var failing atomic.Bool
	failing.Store(true)
	a.client = stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		if failing.Load() {
			http.Error(w, "The power settings could not be read yet: ask again", http.StatusServiceUnavailable)
			return
		}
		answer(t, w, api.PowerBudget{SupplyMilliamps: 500, WhiteMilliamps: 40, IdleMilliamps: 1, MarginPercent: 20, LimitMilliamps: 200})
	})
	// The garland before it left its own model in the engine.
	_ = engine.SetPower(engine.Power{SupplyMilliamps: 5000, WhiteMilliamps: 36, IdleMilliamps: 1, MarginPercent: 10})
	a.power = garlandPower{}
	if a.garlandPowerKnown() {
		t.Fatal("the model of no garland read is taken for this one's")
	}
	wait := func(done func() bool) {
		deadline := time.Now().Add(5 * time.Second)
		for !done() && time.Now().Before(deadline) {
			a.drain()
			time.Sleep(time.Millisecond)
		}
	}
	a.readGarlandPower()
	wait(func() bool { return a.power.client == a.client })
	if a.garlandPowerKnown() || engine.LimitMilliamps() != 4275 {
		t.Fatalf("known %v after a 503, limit %d", a.garlandPowerKnown(), engine.LimitMilliamps())
	}
	// Asked again, as Tune does while it shows none, once the garland can read them.
	failing.Store(false)
	a.power.asked = time.Time{}
	if words := a.powerUnknownText(); !strings.Contains(words, "not shown") {
		t.Fatalf("%q", words)
	}
	wait(a.garlandPowerKnown)
	if !a.garlandPowerKnown() || engine.LimitMilliamps() != 200 {
		t.Fatalf("known %v, limit %d", a.garlandPowerKnown(), engine.LimitMilliamps())
	}
	// With none connected, what was read last stands, for work offline.
	a.client = nil
	if !a.garlandPowerKnown() {
		t.Fatal("no garland connected, and the last model is not taken")
	}
}

// Scheduler24 is offered by a garland with the clock module, and kept on one whose module went since; not by
// one without, nor by a release before it, which says nothing of the module.
func TestScheduler24IsOfferedWithTheClockModuleOnly(t *testing.T) {
	present, absent := true, false
	has := func(display api.Display) bool {
		for _, choice := range saverChoices(display) {
			if choice.key == "scheduler24" {
				return true
			}
		}
		return false
	}
	if !has(api.Display{Saver: "starfield", ClockModule: &present}) || has(api.Display{Saver: "starfield", ClockModule: &absent}) ||
		has(api.Display{Saver: "starfield"}) || !has(api.Display{Saver: "scheduler24", ClockModule: &absent}) {
		t.Fatal("Scheduler24 offered where it should not be, or not where it should")
	}
	if len(savers) != 5 {
		t.Fatal("the list every garland offers changed")
	}
}

// A read of the totals after a new LED count that fails, as on a 503 or a timeout, says nothing of whether the
// garland has the settings: the card and its totals stay, and the error comes back to be said. A 404, read
// as a garland without them, does take the card away.
func TestAFailedRefreshKeepsThePowerCard(t *testing.T) {
	var form powerForm
	form.init()
	before := api.PowerBudget{LimitMilliamps: 1400, Leds: 32, PeakMilliamps: 1312}
	form.takeTotals(before, true)
	failed := errors.New("read the power settings: 503")
	if err := form.takeRefresh(api.PowerBudget{}, false, failed); !errors.Is(err, failed) {
		t.Fatalf("the error was %v", err)
	}
	if !form.held || form.last != before {
		t.Fatalf("held %v, totals %+v", form.held, form.last)
	}
	after := api.PowerBudget{LimitMilliamps: 1400, Leds: 64, PeakMilliamps: 2624}
	if err := form.takeRefresh(after, true, nil); err != nil || form.last != after {
		t.Fatalf("%v, %+v", err, form.last)
	}
	if err := form.takeRefresh(api.PowerBudget{}, false, nil); err != nil || form.held {
		t.Fatalf("a garland that answered 404 keeps the card: %v", err)
	}
}
