// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the walk of the API's routes after an update.
// Contributors: None

package api

import (
	"context"
	"fmt"
	"net/http"
	"strings"
	"testing"
)

// A garland answering as each route's own release has it, with version.
func checkedGarland(t *testing.T, version string, answers map[string]string) *Client {
	t.Helper()
	full := map[string]string{
		"about":             `{"product":"csiGarland","api":1,"version":"` + version + `","image":"aa","boot":"bb"}`,
		"state":             `{"power":true,"brightness":50,"effect":"aurora","speed":1}`,
		"effects":           `{"effects":[{"id":"aurora","name":"Aurora"}]}`,
		"schedule":          `{"entries":[],"revision":1}`,
		"schedule/timeline": `{"synced":true,"now":1,"events":[]}`,
		"playlist":          `{"effects":[],"dwell":60,"cross_fade":1000,"revision":2}`,
		"time":              `{"synced":true,"zone":"UTC0"}`,
		"display":           `{"saver":"starfield"}`,
		"leds/power":        `{"supply_ma":2000,"led_white_ma":40,"led_idle_ma":1,"margin_percent":20,"board_ma":250,"limit_ma":1400,"leds":32,"peak_ma":1312,"undimmed_leds":34}`,
		"firmware":          `{"version":"` + version + `"}`,
		"effects/store":     `{"available":true,"uploaded":0,"max":32,"max_file_bytes":8192}`,
		"wifi":              `{"configured":true,"connected":true,"setup_network":false,"ssid":"home","ip":"10.0.0.2","hostname":"csigarland"}`,
		"wifi/scan":         `{"state":"done","networks":[{"ssid":"home","rssi":-50,"open":false}]}`,
		"info":              `{"model":"Waveshare ESP32-C6-LCD-1.3","chip":"v0.1","ram_total":1,"flash_total":1,"https":true,"effects":98,"power":true}`,
		"clients":           `{"devices":[{"id":0,"label":"this","paired":1,"expires":2,"this":true}]}`,
	}
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		route := strings.TrimPrefix(r.URL.Path, "/api/v1/")
		answer, given := answers[route]
		if !given {
			answer, given = full[route]
		}
		switch {
		case answer == "404":
			http.NotFound(w, r)
		case given:
			_, _ = w.Write([]byte(answer))
		default:
			_, _ = w.Write([]byte(`{}`))
		}
	})
	return client
}

func checksOf(client *Client) map[string]RouteCheck {
	found := map[string]RouteCheck{}
	for _, check := range client.CheckRoutes(context.Background()) {
		found[check.Route] = check
	}
	return found
}

// Every route is held to its type and to the fields it must hold: one of another type, one with a field
// from a newer firmware, one with what it must hold left out, and one not there at all.
func TestEveryRouteIsHeldToItsType(t *testing.T) {
	found := checksOf(checkedGarland(t, "1.1.0.106", map[string]string{
		"state":    `{"power":"yes","brightness":50,"effect":"aurora","speed":1}`,
		"display":  `{"saver":"starfield","delay":60,"glow":3}`,
		"time":     `{"zone":"UTC0","synced":null}`,
		"playlist": "404",
	}))
	if len(found) != len(checkedRoutes) {
		t.Fatalf("%d checks for %d routes", len(found), len(checkedRoutes))
	}
	if !strings.Contains(found["state"].Problem, "power") {
		t.Errorf("state: %+v", found["state"])
	}
	if found["display"].Problem != "" || found["display"].Unknown != "glow" {
		t.Errorf("display: %+v", found["display"])
	}
	if !strings.Contains(found["time"].Problem, "synced is missing") {
		t.Errorf("time: %+v", found["time"])
	}
	// 1.1.0 has the playlist: its 404 is a fault.
	if found["playlist"].Problem == "" || found["playlist"].Absent {
		t.Errorf("playlist: %+v", found["playlist"])
	}
	// 1.1.0 came before the power settings: their 404 is as it should be.
	if found["leds/power"].Problem != "" {
		t.Errorf("leds/power: %+v", found["leds/power"])
	}
	for _, route := range []string{"about", "effects", "schedule/timeline", "info", "clients"} {
		if found[route].Problem != "" || found[route].Unknown != "" {
			t.Errorf("%s: %+v", route, found[route])
		}
	}
	// Every route answering an empty object fails, apart from none: each holds fields it always gives.
	everyEmpty := map[string]string{}
	for _, route := range checkedRoutes {
		everyEmpty[route.route] = `{}`
	}
	for route, check := range checksOf(checkedGarland(t, "1.1.0.106", everyEmpty)) {
		if !strings.Contains(check.Problem, "missing") {
			t.Errorf("an empty %s passed: %+v", route, check)
		}
	}
	// An item of a list with what it must hold left out, apart from a field it may leave out.
	partial := checksOf(checkedGarland(t, "1.1.0.106", map[string]string{
		"schedule/timeline": `{"synced":true,"now":1,"events":[{"at":60,"time":"00:01","day":1,"entries":[0]},{"at":120,"day":1,"entries":[0]}]}`,
		"effects/store":     `{"available":true,"uploaded":0,"max":32}`,
	}))
	if !strings.Contains(partial["schedule/timeline"].Problem, "events[1].time is missing") {
		t.Errorf("an event with no time passed: %+v", partial["schedule/timeline"])
	}
	if !strings.Contains(partial["effects/store"].Problem, "max_file_bytes is missing") {
		t.Errorf("a store with no file limit passed: %+v", partial["effects/store"])
	}
}

// A garland of 1.0.0 answers 404 for what came after it, as it should: no fault.
func TestARouteAfterTheReleaseMayBeAbsent(t *testing.T) {
	found := checksOf(checkedGarland(t, "1.0.0.102", map[string]string{"playlist": "404", "schedule/timeline": "404", "leds/power": "404"}))
	for _, route := range []string{"playlist", "schedule/timeline", "leds/power"} {
		if found[route].Problem != "" || !found[route].Absent {
			t.Errorf("%s: %+v", route, found[route])
		}
	}
}

// A day whose weekday is out of its range is no garland's answer: refused whole, never drawn.
func TestATimelineWithNoSuchWeekdayIsRefused(t *testing.T) {
	for day, refused := range map[int]bool{-1: true, 7: true, 0: false, 6: false} {
		_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
			_, _ = fmt.Fprintf(w, `{"synced":true,"now":1,"events":[{"at":60,"time":"00:01","day":%d,"entries":[0]}]}`, day)
		})
		if _, err := client.Timeline(context.Background()); (err != nil) != refused {
			t.Errorf("day %d: %v", day, err)
		}
	}
}
