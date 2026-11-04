// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the schedule's next 24 hours, the scheduled rise, and the playlist card.
// Contributors: None

package ui

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"reflect"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/ABespalov/csigarland/internal/api"
)

func TestTheDayIsLitAsTheActionsLeaveIt(t *testing.T) {
	const now = 1_000_000_000
	minute := func(m int) int64 { return now + int64(m)*60 }
	events := []api.TimelineEvent{
		{At: minute(60), Power: api.StepOff},
		{At: minute(120), Brightness: 40},
		{At: minute(600), Power: api.StepRise},
		{At: minute(700), Effect: "aurora"},
	}
	if got := LitSpans(events, true, now, now); !reflect.DeepEqual(got, []Span{{0, 60}, {600, dayMinutes}}) {
		t.Fatalf("lit from now: %v", got)
	}
	if got := LitSpans(events, false, now, now); !reflect.DeepEqual(got, []Span{{600, dayMinutes}}) {
		t.Fatalf("dark from now: %v", got)
	}
	if got := LitSpans(nil, true, now, now); !reflect.DeepEqual(got, []Span{{0, dayMinutes}}) {
		t.Fatalf("no actions: %v", got)
	}
	if MinuteOfEvent(api.TimelineEvent{At: now - 600}, now) != 0 || MinuteOfEvent(api.TimelineEvent{At: now + 2*86400}, now) != dayMinutes {
		t.Fatal("an event outside the day is not held to it")
	}
}

func TestATimelineEventSaysWhereActionsMeet(t *testing.T) {
	name := func(id string) string { return map[string]string{"aurora": "Aurora"}[id] }
	words := TimelineText(api.TimelineEvent{Day: 4, Time: "18:00", Entries: []int{0, 2}, Power: api.StepRise, Effect: "aurora"}, name)
	if words != "Thu 18:00: Turn on, rising over a minute, effect Aurora (2 actions meet here)" {
		t.Fatalf("%q", words)
	}
	if words := TimelineText(api.TimelineEvent{Day: 0, Time: "23:30", Entries: []int{1}, Power: api.StepOff}, name); words != "Sun 23:30: Turn off" {
		t.Fatalf("%q", words)
	}
}

func TestTheFormMakesARise(t *testing.T) {
	form := ActionForm{Time: "07:00", Days: allDays, TurnOn: true, Rise: true}
	action, err := BuildAction(form)
	if err != nil || action.Power != api.StepRise {
		t.Fatalf("%+v %v", action, err)
	}
	// Rising means nothing without turning on.
	form.TurnOn = false
	form.UseBrightness, form.Brightness = true, "30"
	if action, _ := BuildAction(form); action.Power != "" {
		t.Fatalf("%+v", action)
	}
}

// The card reads the garland's playlist, and Save sends it whole, as edited.
func TestThePlaylistIsReadAndSaved(t *testing.T) {
	sent := make(chan map[string]any, 1)
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		switch {
		case r.URL.Path == "/api/v1/playlist" && r.Method == http.MethodPost:
			var body map[string]any
			_ = json.NewDecoder(r.Body).Decode(&body)
			sent <- body
			answer(t, w, api.Playlist{Effects: []string{"aurora"}, Dwell: 90, CrossFade: 2000, Max: 16})
		case r.URL.Path == "/api/v1/playlist":
			answer(t, w, api.Playlist{Effects: []string{"aurora", "fireplace"}, Dwell: 60, CrossFade: 1000, Max: 16})
		default:
			http.NotFound(w, r)
		}
	})
	a := newApp("1.0.0")
	defer a.stop()
	a.client, a.connected = client, true
	page := &a.effects
	page.loadPlaylist(a)
	untilTrue(t, a, func() bool { return page.playlist.read })
	if strings.Join(page.playlist.list.Effects, ",") != "aurora,fireplace" {
		t.Fatalf("%+v", page.playlist.list)
	}
	page.playlist.list.Effects = []string{"aurora"}
	page.playlist.list.Dwell = 90
	page.sendPlaylist(a, func(ctx context.Context, client *api.Client) (api.Playlist, error) {
		return client.SetPlaylist(ctx, page.playlist.list.Effects, page.playlist.list.Dwell, 2000)
	}, "saved", false)
	body := <-sent
	if body["dwell"] != 90.0 || body["cross_fade"] != 2000.0 || len(body["effects"].([]any)) != 1 {
		t.Fatalf("%v", body)
	}
	untilTrue(t, a, func() bool { return !page.playlist.sending })
}

func TestTheRailSaysWhereThePlaylistStands(t *testing.T) {
	cases := map[string]api.State{
		"Off":                       {EffectName: "Aurora"},
		"Aurora":                    {Power: true, EffectName: "Aurora"},
		"Playlist 3/12 (Fireplace)": {Power: true, EffectName: "Fireplace", Playlist: true, PlaylistAt: 3, PlaylistSize: 12},
	}
	for want, state := range cases {
		if got := PlayingText(state); got != want {
			t.Errorf("%+v: %q", state, got)
		}
	}
}

// After a restart into a firmware on trial, the footer counts down to its confirmation, and says it.
func TestTheFooterCountsDownATrial(t *testing.T) {
	a := newApp("1.0.0")
	defer a.stop()
	a.takeAbout(api.About{Version: "1.0.0.103", OnTrial: true, TrialLeft: 42})
	if a.status != "The firmware is on trial: it confirms itself within 42 s..." {
		t.Fatalf("%q", a.status)
	}
	a.takeAbout(api.About{Version: "1.0.0.103"})
	if a.status != "The new firmware confirmed itself." {
		t.Fatalf("%q", a.status)
	}
	a.takeAbout(api.About{Version: "1.0.0.103"})
	if a.status != "The new firmware confirmed itself." {
		t.Fatalf("a firmware long confirmed is told again: %q", a.status)
	}
}

// A trial that ends on another image is a going back, said as such, not a confirmation.
func TestATrialEndingOnAnotherImageIsAGoingBack(t *testing.T) {
	a := newApp("1.0.0")
	defer a.stop()
	a.takeAbout(api.About{Version: "1.1.0.106", Image: "new", OnTrial: true, TrialLeft: 30})
	a.takeAbout(api.About{Version: "1.1.0.105", Image: "old"})
	if !a.failed || !strings.Contains(a.status, "went back to 1.1.0.105") {
		t.Fatalf("%q, failed %v", a.status, a.failed)
	}
	a.takeAbout(api.About{Version: "1.1.0.106", Image: "new", OnTrial: true, TrialLeft: 30})
	a.takeAbout(api.About{Version: "1.1.0.106", Image: "new"})
	if a.failed || a.status != "The new firmware confirmed itself." {
		t.Fatalf("%q", a.status)
	}
}
func TestTheFormStartsThePlaylist(t *testing.T) {
	action, err := BuildAction(ActionForm{Time: "18:00", Days: allDays, UsePlaylist: true})
	if err != nil || !action.Playlist || action.Effect != "" {
		t.Fatalf("%+v %v", action, err)
	}
	if words := StepsText(api.StepOn, 0, 0, "", true); words != "Turn on, start the playlist" {
		t.Fatalf("%q", words)
	}
}

// A state the garland pushes reaches the window, which then asks for none.
func TestAPushedStateReachesTheWindow(t *testing.T) {
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/v1/events" {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "text/event-stream")
		_, _ = io.WriteString(w, "event: state\ndata: {\"power\":true,\"effect\":\"aurora\",\"effect_name\":\"Aurora\",\"playlist\":true,\"playlist_at\":2,\"playlist_size\":4}\n\n")
		w.(http.Flusher).Flush()
		<-r.Context().Done()
	})
	a := newApp("1.0.0")
	defer a.stop()
	a.client, a.connected = client, true
	a.listen()
	untilTrue(t, a, func() bool { return a.pushing && a.state.EffectName == "Aurora" })
	if PlayingText(a.state) != "Playlist 2/4 (Aurora)" {
		t.Fatalf("%q", PlayingText(a.state))
	}
	// The About asked for as the stream opened failed here: it is asked for again, with the next look.
	if !a.aboutStale {
		t.Fatal("a failed About after the stream opened is not asked for again")
	}
	a.takeAbout(api.About{Version: "1.1.0.103"})
	asked := a.stateAsked
	a.refreshState()
	if a.stateAsked != asked {
		t.Fatal("the window asked for a state while the stream brings it")
	}
}

// The next 24 hours read while a list is saved are older than it, and asked for again; a garland with
// no such route is not asked again, and a reading that failed keeps what was there.
func TestTheDayIsReadAgainAfterASave(t *testing.T) {
	var asks atomic.Int32
	release := make(chan struct{}, 4)
	var status atomic.Int32
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "/time") {
			answer(t, w, api.Clock{Synced: true, Now: "2026-11-03 18:00"})
			return
		}
		n := asks.Add(1)
		<-release
		if code := int(status.Load()); code != 0 {
			http.Error(w, "no", code)
			return
		}
		answer(t, w, api.Timeline{Synced: true, Now: int64(n)})
	})
	a := sceneApp(t, PageSchedule)
	a.client = client
	p := &a.schedule
	p.timeline, p.timelineAsked = nil, false
	p.loadTimeline(a)
	untilTrue(t, a, func() bool { return asks.Load() == 1 })
	// A save, while the first reading is on its way.
	p.loadTimeline(a)
	release <- struct{}{}
	untilTrue(t, a, func() bool { return asks.Load() == 2 })
	release <- struct{}{}
	untilTrue(t, a, func() bool { return !p.timelineAsked && p.timeline != nil })
	if p.timeline.Now != 2 {
		t.Fatalf("took the reading from before the save: %d", p.timeline.Now)
	}
	// The clock the card goes by comes with the day.
	if !p.clock.Synced || p.clock.Now != "2026-11-03 18:00" {
		t.Fatalf("clock %+v", p.clock)
	}
	// A failure keeps the day, and the time it is drawn by, and is tried again soon.
	read := time.Now().Add(-5 * time.Minute)
	p.timelineAt = read
	status.Store(http.StatusServiceUnavailable)
	p.loadTimeline(a)
	release <- struct{}{}
	untilTrue(t, a, func() bool { return !p.timelineAsked })
	if p.timeline == nil || !p.timelineFailed || !p.timelineAt.Equal(read) {
		t.Fatalf("timeline %v, failed %v, read at %v for %v", p.timeline, p.timelineFailed, p.timelineAt, read)
	}
	// 1.0.0 has no such route: nothing is drawn, and nothing asked again.
	status.Store(http.StatusNotFound)
	p.loadTimeline(a)
	release <- struct{}{}
	untilTrue(t, a, func() bool { return !p.timelineAsked })
	p.timelineTried = time.Time{}
	p.keepTimeline(a)
	if p.timeline != nil || !p.timelineNone || p.timelineAsked {
		t.Fatalf("timeline %v, none %v, asked %v", p.timeline, p.timelineNone, p.timelineAsked)
	}
	// Another firmware since, as 1.1.0 installed on that 1.0.0: its day is asked for again.
	a.takeAbout(api.About{Version: "1.1.0.107", Image: "another"})
	if p.timelineNone {
		t.Fatal("a garland updated since is still taken for one with no day")
	}
}

// The day's own time is when the day came, though the clock read after it is slow; and a clock that
// fails to read leaves the card saying the clock is set, as the day says.
func TestTheDayKeepsItsOwnTimeAndClock(t *testing.T) {
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "/time") {
			time.Sleep(300 * time.Millisecond)
			http.Error(w, "busy", http.StatusServiceUnavailable)
			return
		}
		answer(t, w, api.Timeline{Synced: true, Now: 1_800_000_000})
	})
	a := sceneApp(t, PageSchedule)
	a.client = client
	p := &a.schedule
	p.timeline, p.timelineAsked, p.clock = nil, false, api.Clock{}
	asked := time.Now()
	p.loadTimeline(a)
	untilTrue(t, a, func() bool { return !p.timelineAsked && p.timeline != nil })
	if p.timelineAt.Sub(asked) >= 250*time.Millisecond {
		t.Fatalf("the day's time waited for the clock: %v", p.timelineAt.Sub(asked))
	}
	if words := p.clockText(); words != "The garland's clock is set." {
		t.Fatalf("%q", words)
	}
}

// Of the past events of the next 24 hours, none is drawn again: the garland's state says what they did,
// and what the owner did since.
func TestPastEventsAreLeftOutOfTheDay(t *testing.T) {
	now := int64(1_800_000_000)
	events := []api.TimelineEvent{{At: now - 600, Power: api.StepOff}, {At: now + 3600, Power: api.StepOff}}
	spans := LitSpans(events, true, now, now)
	if len(spans) != 1 || spans[0] != (Span{0, 60}) {
		t.Fatalf("%+v", spans)
	}
	if left := Upcoming(events, now, now); len(left) != 1 || left[0].At != now+3600 {
		t.Fatalf("%+v", left)
	}
	// A step the garland's scheduler had yet to do when the day was read is drawn from the start, while no
	// newer state has come; once one has, that state says what it did, and the step is past.
	pending := []api.TimelineEvent{{At: now - 75, Power: api.StepOff}, {At: now + 3600, Power: api.StepOn}}
	read := time.Unix(now, 0)
	from := PendingFrom(now, read, read.Add(-time.Second), nil, true, 0)
	if left := Upcoming(pending, now, from); len(left) != 2 {
		t.Fatalf("a step still to do was left out: %+v", left)
	}
	for _, span := range LitSpans(pending, true, now, from) {
		if span.From == 0 && span.To > 0 {
			t.Fatalf("drawn lit before an off still to come: %+v", span)
		}
	}
	after := PendingFrom(now, read, read.Add(time.Second), nil, true, 0)
	if left := Upcoming(pending, now, after); len(left) != 1 {
		t.Fatalf("a step done, as a newer state says, is drawn again: %+v", left)
	}
	if spans := LitSpans(pending, true, now, after); len(spans) == 0 || spans[0].From != 0 {
		t.Fatalf("the state come since is not what the day starts from: %+v", spans)
	}
	// With the day's own word on the power: a state come later, still lit as then, keeps the step still to
	// do; one that is dark now, the step done, drops it.
	lit, dark := true, false
	if PendingFrom(now, read, read.Add(time.Second), &lit, true, 0) != from {
		t.Fatal("a later state of the same power dropped a step still to do")
	}
	if PendingFrom(now, read, read.Add(time.Second), &lit, false, 0) != now {
		t.Fatal("a step done, the power changed, is still drawn")
	}
	if PendingFrom(now, read, read.Add(-time.Second), &dark, true, 0) != now {
		t.Fatal("a power other than the day's keeps its steps")
	}
}

// A step done, and the owner turning the garland on again before the next look: the power is as when the
// day was read, and only the scheduler's own word says the step is past.
func TestTheSchedulersWordOnWhatItHasDoneDecides(t *testing.T) {
	now := int64(1_800_000_000)
	read := time.Unix(now-120, 0)
	lit := true
	events := []api.TimelineEvent{{At: now - 60, Power: api.StepOff}, {At: now + 3600, Power: api.StepOff}}
	done := PendingFrom(now, read, read.Add(time.Second), &lit, true, now)
	if left := Upcoming(events, now, done); len(left) != 1 || left[0].At != now+3600 {
		t.Fatalf("a step done is drawn again: %+v", left)
	}
	if spans := LitSpans(events, true, now, done); len(spans) != 1 || spans[0] != (Span{0, 60}) {
		t.Fatalf("%+v", spans)
	}
	// Not done yet, as the scheduler says: still to come.
	if left := Upcoming(events, now, PendingFrom(now, read, read.Add(time.Second), &lit, true, now-60)); len(left) != 2 {
		t.Fatalf("a step still to do was left out: %+v", left)
	}
}

// With no stream, as with both of the garland's taken elsewhere, the window still asks now and then what
// the garland says of itself, as an update sent from elsewhere restarts it with nothing else to say so.
func TestWithNoStreamAboutIsAskedNowAndThen(t *testing.T) {
	var abouts atomic.Int32
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		if strings.HasSuffix(r.URL.Path, "/about") {
			abouts.Add(1)
			answer(t, w, api.About{Version: "1.1.0.104"})
			return
		}
		answer(t, w, api.State{Effect: "aurora"})
	})
	a := newApp("1.0.0")
	defer a.stop()
	a.client, a.connected = client, true
	a.about = api.About{Version: "1.1.0.103"}
	a.aboutAt = time.Now().Add(-aboutEvery)
	a.refreshState()
	untilTrue(t, a, func() bool { return !a.stateAsked })
	if abouts.Load() != 1 || a.about.Version != "1.1.0.104" {
		t.Fatalf("asked %d, version %q", abouts.Load(), a.about.Version)
	}
	a.refreshState()
	untilTrue(t, a, func() bool { return !a.stateAsked })
	if abouts.Load() != 1 {
		t.Fatal("About was asked for at every look")
	}
}
