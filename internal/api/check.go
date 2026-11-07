// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: A walk of every route of /api/v1/ that only reads, each answer held to the types here.
// Contributors: None

package api

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"strings"
)

// RouteCheck is what one route's answer came to.
type RouteCheck struct {
	Route string
	// Problem is what is wrong: no answer, or a field of another type than this utility reads. Empty
	// when the answer reads as this utility reads it.
	Problem string
	// Unknown is a field this utility does not know, from a newer firmware: worth knowing, no fault.
	Unknown string
	// Absent says the route came after the firmware's release, which answers 404 for it, as it should.
	Absent bool
}

// checkedRoutes are the routes that change nothing and need no parameter, each with the type its answer
// is read into, the fields it must hold, those each item of a list in it must hold, and the release it
// came with. Every field required is one each firmware since that release always writes, 1.0.0's
// included. The log is left out, as reading it starts the garland keeping it, and so is the stream of
// events, which never ends.
var checkedRoutes = []struct {
	route    string
	since    [3]int
	required []string
	items    map[string][]string
	into     func() any
}{
	{"about", [3]int{1, 0, 0}, []string{"product", "api", "version", "image", "boot"}, nil, func() any { return &About{} }},
	{"state", [3]int{1, 0, 0}, []string{"power", "brightness", "effect", "speed"}, nil, func() any { return &State{} }},
	{"effects", [3]int{1, 0, 0}, []string{"effects"}, map[string][]string{"effects": {"id", "name"}}, func() any { return &EffectList{} }},
	{"effects/store", [3]int{1, 0, 0}, []string{"available", "uploaded", "max", "max_file_bytes"}, nil, func() any { return &EffectStore{} }},
	{"schedule", [3]int{1, 0, 0}, []string{"entries", "revision"}, map[string][]string{"entries": {"enabled", "time", "days"}}, func() any { return &Schedule{} }},
	{"schedule/timeline", [3]int{1, 1, 0}, []string{"synced", "now", "events"}, map[string][]string{"events": {"at", "time", "day", "entries"}}, func() any { return &Timeline{} }},
	{"playlist", [3]int{1, 1, 0}, []string{"effects", "dwell", "cross_fade", "revision"}, nil, func() any { return &Playlist{} }},
	{"time", [3]int{1, 0, 0}, []string{"synced", "zone"}, nil, func() any { return &Clock{} }},
	{"display", [3]int{1, 0, 0}, []string{"saver"}, nil, func() any { return &Display{} }},
	{"leds/power", [3]int{1, 2, 0}, []string{"supply_ma", "led_white_ma", "led_idle_ma", "margin_percent", "limit_ma"}, nil, func() any { return &PowerBudget{} }},
	{"wifi", [3]int{1, 0, 0}, []string{"configured", "connected", "setup_network", "hostname"}, nil, func() any { return &Wifi{} }},
	{"wifi/scan", [3]int{1, 0, 0}, []string{"state", "networks"}, map[string][]string{"networks": {"ssid", "rssi"}}, func() any { return &Scan{} }},
	{"firmware", [3]int{1, 0, 0}, []string{"version"}, nil, func() any { return &Firmware{} }},
	{"info", [3]int{1, 0, 0}, []string{"model", "chip", "ram_total", "flash_total", "https", "effects", "power"}, nil, func() any { return &Info{} }},
	{"clients", [3]int{1, 0, 0}, []string{"devices"}, map[string][]string{"devices": {"id", "label"}}, func() any { return &clientList{} }},
}

// releaseOf is the release a version belongs to, as 1.1.0 of 1.1.0.106-dev; a version it cannot read
// counts as the newest, which has every route.
func releaseOf(version string) [3]int {
	release := [3]int{1 << 30, 0, 0}
	parts := strings.FieldsFunc(version, func(r rune) bool { return r == '.' || r == '-' })
	if len(parts) < 3 {
		return release
	}
	for i := range 3 {
		number, err := strconv.Atoi(parts[i])
		if err != nil {
			return [3]int{1 << 30, 0, 0}
		}
		release[i] = number
	}
	return release
}

// HasRoute says whether a firmware of version has route, one of those checkedRoutes names, by the release
// it came with: a garland of 1.0.0 has no playlist. One it does not know is taken as there.
func HasRoute(version, route string) bool {
	for _, known := range checkedRoutes {
		if known.route == route {
			return !before(releaseOf(version), known.since)
		}
	}
	return true
}

func before(release, since [3]int) bool {
	for i := range 3 {
		if release[i] != since[i] {
			return release[i] < since[i]
		}
	}
	return false
}

// CheckRoutes reads every route of checkedRoutes, one after another, and says of each how its answer
// reads. A route the firmware's release has is a problem when it fails; one that came later is fine
// answering 404, as a 1.0.0 garland does for the playlist. The release is the one About names.
func (c *Client) CheckRoutes(ctx context.Context) []RouteCheck {
	checks := make([]RouteCheck, 0, len(checkedRoutes))
	release := releaseOf("")
	for _, route := range checkedRoutes {
		check := RouteCheck{Route: route.route}
		response, err := c.request(ctx, http.MethodGet, route.route, nil, nil, "")
		var failure *Error
		switch {
		case err == nil:
			var version string
			check.Problem, check.Unknown, version = readStrictly(response, route.into, route.required, route.items)
			if route.route == "about" && version != "" {
				release = releaseOf(version)
			}
		case before(release, route.since) && errors.As(err, &failure) && failure.Status == http.StatusNotFound:
			check.Absent = true
		default:
			check.Problem = err.Error()
		}
		checks = append(checks, check)
	}
	return checks
}

// readStrictly reads an answer as the utility does, again refusing a field it does not know, and holds
// it to the fields it must have: a field left out, or null, reads as its zero and would pass for one.
// The version an answer names, if it names one.
func readStrictly(response *http.Response, into func() any, required []string, items map[string][]string) (problem, unknown, version string) {
	defer response.Body.Close()
	data, err := io.ReadAll(io.LimitReader(response.Body, maxAnswerBytes))
	if err != nil {
		return fmt.Sprintf("read the answer: %v", err), "", ""
	}
	var fields map[string]json.RawMessage
	if err := json.Unmarshal(data, &fields); err != nil || fields == nil {
		return "the answer is not an object", "", ""
	}
	if problem := missingFrom(fields, required, ""); problem != "" {
		return problem, "", ""
	}
	// An item of a list held to the same: an event with no time would be drawn at midnight.
	for list, needs := range items {
		var held []map[string]json.RawMessage
		if raw, found := fields[list]; found && string(raw) != "null" {
			if err := json.Unmarshal(raw, &held); err != nil {
				return fmt.Sprintf("%s is not a list of objects", list), "", ""
			}
		}
		for i, item := range held {
			if problem := missingFrom(item, needs, fmt.Sprintf("%s[%d].", list, i)); problem != "" {
				return problem, "", ""
			}
		}
	}
	if raw, held := fields["version"]; held {
		_ = json.Unmarshal(raw, &version)
	}
	problem, unknown = readTyped(data, into)
	return problem, unknown, version
}

// missingFrom names the first of names that fields lacks, or holds as null, as prefix then the name.
func missingFrom(fields map[string]json.RawMessage, names []string, prefix string) string {
	for _, name := range names {
		if value, held := fields[name]; !held || string(value) == "null" {
			return fmt.Sprintf("%s%s is missing", prefix, name)
		}
	}
	return ""
}

func readTyped(data []byte, into func() any) (problem, unknown string) {
	if err := json.Unmarshal(data, into()); err != nil {
		var wrongType *json.UnmarshalTypeError
		if errors.As(err, &wrongType) {
			return fmt.Sprintf("%s is a %s, read as %s", wrongType.Field, wrongType.Value, wrongType.Type), ""
		}
		return err.Error(), ""
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(into()); err != nil {
		if field, found := strings.CutPrefix(err.Error(), "json: unknown field "); found {
			return "", strings.Trim(field, `"`)
		}
		return err.Error(), ""
	}
	return "", ""
}
