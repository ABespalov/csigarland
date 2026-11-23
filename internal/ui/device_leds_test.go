// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the Device page's LED count: set by hand only, and what it says of a garland.
// Contributors: None

package ui

import (
	"testing"

	"github.com/ABespalov/csigarland/internal/api"
)

// The count is set by hand; with none set the firmware's own number is used, and a garland of 1.4.0 or
// before that still estimated one is told as it was.
func TestLedsTextSaysWhereTheCountComesFrom(t *testing.T) {
	cases := []struct {
		leds api.Leds
		want string
	}{
		{api.Leds{Setting: 32, Count: 32, Probe: "present"}, "Using 32 LEDs."},
		{api.Leds{Setting: 32, Count: 32, Probe: "absent"}, "Using 32 LEDs. Nothing is on the data line."},
		{api.Leds{Count: 32, Probe: "present"}, "Using 32 LEDs, the firmware's own number: set yours below."},
		{api.Leds{Count: 32, Probe: "absent"}, "Using 32 LEDs, the firmware's own number: set yours below. Nothing is on the data line."},
		{api.Leds{Count: 30, Probe: "present", Estimate: 30}, "Using about 30 LEDs, as detected."},
		{api.Leds{Setting: 100, Count: 100, Kept: 32}, "Could not change to 100 LEDs. Still sending to 32: restart the garland, or choose another number."},
	}
	for _, c := range cases {
		if got := LedsText(c.leds); got != c.want {
			t.Errorf("LedsText(%+v) = %q, want %q", c.leds, got, c.want)
		}
	}
}

// A count below 1 is refused before anything is sent: 0 would ask the garland for its own number.
func TestTheLedCountIsANumberFromOne(t *testing.T) {
	a := sceneApp(t, PageDevice)
	var page devicePage
	page.init()
	for _, text := range []string{"", "x", "0", "-3"} {
		page.count.SetText(text)
		a.status, a.failed = "", false
		page.saveCount(a)
		if !a.failed || a.status == "" {
			t.Errorf("a count of %q was not refused", text)
		}
	}
}
