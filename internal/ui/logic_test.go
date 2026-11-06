// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of what the window works out.
// Contributors: None

package ui

import (
	"runtime"
	"strings"
	"testing"
	"unicode/utf8"

	"github.com/ABespalov/csigarland/internal/api"
)

func TestGroupsKeepTheirOrderWithUploadedLast(t *testing.T) {
	groups := GroupEffects([]api.Effect{
		{ID: "a", Group: "One", Favorite: true},
		{ID: "mine", Group: UploadedGroup},
		{ID: "b", Group: "Two"},
		{ID: "c", Group: "One"},
	})
	if len(groups) != 3 || groups[0].Name != "One" || groups[1].Name != "Two" || groups[2].Name != UploadedGroup {
		t.Fatalf("%+v", groups)
	}
	if GroupTitle(groups[0]) != "One (1 of 2)" || GroupTitle(groups[1]) != "Two (1)" {
		t.Errorf("%q, %q", GroupTitle(groups[0]), GroupTitle(groups[1]))
	}
	empty := GroupEffects([]api.Effect{{ID: "a", Group: "One"}})
	if empty[len(empty)-1].Name != UploadedGroup || GroupTitle(empty[len(empty)-1]) != "Uploaded (0)" {
		t.Errorf("no uploads: %+v", empty)
	}
}

func TestSlidersSendWhatTheGarlandTakes(t *testing.T) {
	if SliderToSpeed(0) != 0.25 || SliderToSpeed(1) != 5 {
		t.Errorf("speed ends: %v, %v", SliderToSpeed(0), SliderToSpeed(1))
	}
	for quarters := 1; quarters <= 20; quarters++ {
		speed := float64(quarters) / 4
		if got := SliderToSpeed(SpeedToSlider(speed)); got != speed {
			t.Errorf("speed %v came back %v", speed, got)
		}
	}
	for percent := 1; percent <= 100; percent++ {
		if got := SliderToPercent(PercentToSlider(percent)); got != percent {
			t.Errorf("brightness %d came back %d", percent, got)
		}
	}
	if SpeedText(0.25) != "0.25x" || SpeedText(1) != "1x" || SpeedText(1.5) != "1.5x" {
		t.Error(SpeedText(0.25), SpeedText(1), SpeedText(1.5))
	}
}

func TestActionsAreSaidAsThePortalSaysThem(t *testing.T) {
	if DaysText(127) != "every day" || DaysText(1|1<<6) != "Sun, Sat" {
		t.Error(DaysText(127), DaysText(65))
	}
	if StepsText("off", 40, 1, "x", false) != "Turn off" {
		t.Error("off takes nothing with it")
	}
	if got := StepsText("on", 75, 0.5, "Fire", false); got != "Turn on, brightness 75%, speed 0.5x, effect Fire" {
		t.Error(got)
	}
	if NextText(api.NextAction{}) != "Waiting for the clock to be set." || NextText(api.NextAction{Clock: true}) != "Nothing in the coming week." {
		t.Error("next")
	}
	if got := NextText(api.NextAction{Clock: true, At: "Fri 25 Dec, 23:30", Power: "off"}); got != "Fri 25 Dec, 23:30: Turn off." {
		t.Error(got)
	}
}

func TestTheFormIsCheckedAsThePortalDoes(t *testing.T) {
	good := ActionForm{Enabled: true, Time: "18:00", Days: 127, TurnOn: true}
	if action, err := BuildAction(good); err != nil || action.Power != api.StepOn {
		t.Fatalf("%+v, %v", action, err)
	}
	off := good
	off.TurnOff = true
	off.UseBrightness = true
	off.Brightness = "40"
	if action, err := BuildAction(off); err != nil || action.Power != api.StepOff || action.Brightness != 0 {
		t.Errorf("nothing goes with off: %+v, %v", action, err)
	}
	for name, form := range map[string]ActionForm{
		"time":       {Time: "25:00", Days: 127, TurnOn: true},
		"bare time":  {Time: "1800", Days: 127, TurnOn: true},
		"no day":     {Time: "18:00", TurnOn: true},
		"no step":    {Time: "18:00", Days: 127},
		"brightness": {Time: "18:00", Days: 127, UseBrightness: true, Brightness: "101"},
		"not number": {Time: "18:00", Days: 127, UseBrightness: true, Brightness: "x"},
		"no effect":  {Time: "18:00", Days: 127, UseEffect: true},
	} {
		if _, err := BuildAction(form); err == nil {
			t.Errorf("%s: taken", name)
		}
	}
}

func TestAFilesNameIsTheEffectsID(t *testing.T) {
	for path, want := range map[string]string{
		`C:\Users\me\My_Effect.json`: "my-effect",
		"/home/me/snow-2.json":       "snow-2",
	} {
		if got, err := EffectIDFromPath(path); err != nil || got != want {
			t.Errorf("%q: %q, %v", path, got, err)
		}
	}
	for _, path := range []string{"snow flake.json", ".json", strings.Repeat("a", 33) + ".json"} {
		if got, err := EffectIDFromPath(path); err == nil {
			t.Errorf("%q gave %q", path, got)
		}
	}
}

func TestALogIsSplitIntoLinesAcrossReads(t *testing.T) {
	var log LogLines
	log.Add("I (1) one\nI (2) t", 0)
	log.Add("wo\n", 0)
	if len(log.Lines) != 2 || log.Lines[1] != "I (2) two" {
		t.Fatalf("%q", log.Lines)
	}
	log.Add("I (3) x\r\n", 500)
	if !strings.Contains(log.Lines[2], "500 bytes were lost") || log.Lines[3] != "I (3) x" {
		t.Errorf("%q", log.Lines)
	}
	for i := 0; i < maxLogLines+50; i++ {
		log.Add("line\n", 0)
	}
	if len(log.Lines) != maxLogLines {
		t.Errorf("%d lines kept", len(log.Lines))
	}
	log.Clear()
	if len(log.Lines) != 0 {
		t.Error("not cleared")
	}
}

func TestALabelFitsWhatTheFirmwareKeeps(t *testing.T) {
	if label := ClientLabel(strings.Repeat("x", 60)); len(label) > 23 {
		t.Errorf("%d bytes", len(label))
	}
	if label := ClientLabel(strings.Repeat("я", 30) /* Two bytes each in UTF-8. */); !utf8.ValidString(label) || len(label) > 23 {
		t.Errorf("%q", label)
	}
	if ClientLabel("") == "" {
		t.Error("no label")
	}
	if label := ClientLabel("Studio.local"); label != "csiGarland on Studio" {
		t.Errorf("a Mac's name: %q", label)
	}
	if label := ClientLabel(".local"); label != "csiGarland on "+systemName(runtime.GOOS) {
		t.Errorf("no name left: %q", label)
	}
	if systemName("darwin") != "macOS" || systemName("linux") != "linux" {
		t.Error(systemName("darwin"), systemName("linux"))
	}
}

func TestAnUpdateIsDescribed(t *testing.T) {
	if got := UpdateText(api.UpdateFile{HasFirmware: true, Version: "0.1", HasEffects: true, Effects: 154}); got != "This file holds firmware 0.1 and 154 effects." {
		t.Error(got)
	}
	if got := UpdateText(api.UpdateFile{HasEffects: true, Effects: -1}); got != "This file holds a newer set of effects." {
		t.Error(got)
	}
}

func TestZonesAreNamed(t *testing.T) {
	if ZoneName("MSK-3") != "Moscow, Minsk" || ZoneName("XYZ1") != "XYZ1" {
		t.Error(ZoneName("MSK-3"), ZoneName("XYZ1"))
	}
}

// POSIX counts west of UTC, and a clock is labelled east of it.
func TestAZonesOffsetsAreThoseOfItsClocks(t *testing.T) {
	for posix, want := range map[string][3]int{
		"UTC0":                         {0, 0, 0},
		"MSK-3":                        {180, 0, 0},
		"<+04>-4":                      {240, 0, 0},
		"<+0530>-5:30":                 {330, 0, 0},
		"EST5EDT,M3.2.0,M11.1.0":       {-300, -240, 1},
		"GMT0BST,M3.5.0/1,M10.5.0":     {0, 60, 1},
		"IST-2IDT,M3.4.4/26,M10.5.0":   {120, 180, 1},
		"CET-1CEST,M3.5.0,M10.5.0/3":   {60, 120, 1},
		"LHST-10:30LHDT-11,M10.1.0,M4": {630, 660, 1},
	} {
		standard, summer, hasSummer, err := ZoneOffsets(posix)
		if err != nil || standard != want[0] || summer != want[1] || hasSummer != (want[2] == 1) {
			t.Errorf("%s: %d, %d, %v, %v", posix, standard, summer, hasSummer, err)
		}
	}
	for _, zone := range Zones {
		if _, _, _, err := ZoneOffsets(zone[1]); err != nil {
			t.Errorf("%s: %v", zone[0], err)
		}
	}
	for _, bad := range []string{"", "X1", "<>3", "UTC"} {
		if _, _, _, err := ZoneOffsets(bad); err == nil {
			t.Errorf("%q was read", bad)
		}
	}
	if got := ZoneLabel("<+04>-4"); got != "UTC+04:00 Dubai, Yerevan, Tbilisi, Baku" {
		t.Error(got)
	}
	if got := ZoneLabel("EST5EDT,M3.2.0,M11.1.0"); got != "UTC-05:00 New York (summer UTC-04:00)" {
		t.Error(got)
	}
	if got := ZoneLabel("XYZ"); got != "XYZ" {
		t.Error(got)
	}
}
