// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: What happens over time: answers against a stub garland, in the order they come back.
// Contributors: None

package ui

import (
	"context"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"image"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"gioui.org/io/input"
	"gioui.org/op"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/settings"
)

func stubGarland(t *testing.T, handler http.HandlerFunc) *api.Client {
	t.Helper()
	garland := httptest.NewTLSServer(handler)
	t.Cleanup(garland.Close)
	root := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: garland.Certificate().Raw})
	client, err := api.New(garland.URL, root, "a-token")
	if err != nil {
		t.Fatalf("client: %v", err)
	}
	t.Cleanup(client.Close)
	return client
}

func answer(t *testing.T, w http.ResponseWriter, value any) {
	t.Helper()
	if err := json.NewEncoder(w).Encode(value); err != nil {
		t.Errorf("answer: %v", err)
	}
}

// Until what was queued so far has gone, and its answer has reached the window.
func settleWork(t *testing.T, a *App) {
	t.Helper()
	done := make(chan struct{})
	a.work(func(context.Context, *api.Client) (func(), error) {
		return func() { close(done) }, nil
	})
	deadline := time.Now().Add(10 * time.Second)
	for time.Now().Before(deadline) {
		a.drain()
		select {
		case <-done:
			return
		default:
		}
		time.Sleep(time.Millisecond)
	}
	t.Fatal("the queue never emptied")
}

// The worker ends, and ends its session, ahead of the window's next look at what it said.
func TestAnUpdatesLastWordsReachTheWindowAfterItsWorkerHasEnded(t *testing.T) {
	file := api.UpdateFile{HasFirmware: true, Image: "new"}
	cases := map[string]struct {
		before int
		post   int
		now    api.Firmware
		words  string
		failed bool
	}{
		"a failure before sending": {before: http.StatusInternalServerError, words: "not installed", failed: true},
		"a refusal":                {post: http.StatusBadRequest, words: "not installed", failed: true},
		"confirmed":                {now: api.Firmware{Boot: "after", Image: "new"}, words: "confirmed"},
		"gone back":                {now: api.Firmware{Boot: "after", Image: "old", Version: "1"}, words: "went back", failed: true},
	}
	for name, c := range cases {
		t.Run(name, func(t *testing.T) {
			var sent atomic.Bool
			client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
				switch {
				case r.Method == http.MethodPost && c.post != 0:
					http.Error(w, "refused", c.post)
				case r.Method == http.MethodPost:
					sent.Store(true)
					answer(t, w, map[string]bool{"ok": true})
				case c.before != 0:
					http.Error(w, "broken", c.before)
				case sent.Load():
					answer(t, w, c.now)
				default:
					answer(t, w, api.Firmware{Boot: "before", Image: "old"})
				}
			})
			a := sceneApp(t, PageFirmware)
			a.client = client
			a.firmware.file = []byte("update")
			a.firmware.update = file
			a.firmware.send(a)
			select {
			case <-a.firmware.session.Done():
			case <-time.After(20 * time.Second):
				t.Fatal("the worker did not end")
			}
			a.drain()
			if a.firmware.busy || a.firmware.uploading {
				t.Fatalf("busy %v, uploading %v: the window stays locked", a.firmware.busy, a.firmware.uploading)
			}
			if !strings.Contains(a.status, c.words) || a.failed != c.failed {
				t.Fatalf("%q, failed %v", a.status, a.failed)
			}
		})
	}
}

func TestLeavingDuringAnUpdateDropsWhatItSays(t *testing.T) {
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "broken", http.StatusInternalServerError)
	})
	a := sceneApp(t, PageFirmware)
	a.client = client
	a.firmware.file = []byte("update")
	a.firmware.send(a)
	session := a.firmware.session
	a.client = nil
	a.disconnect()
	<-session.Done()
	time.Sleep(50 * time.Millisecond)
	a.status = ""
	a.drain()
	if a.status != "" || a.firmware.busy {
		t.Fatalf("%q, busy %v", a.status, a.firmware.busy)
	}
}

func TestTheBarSaysHowFarAnUploadHasGone(t *testing.T) {
	if UploadText(42) != "Sending: 42%" || !strings.Contains(UploadText(100), "Waiting") {
		t.Fatalf("%q %q", UploadText(42), UploadText(100))
	}
	a := sceneApp(t, PageFirmware)
	a.firmware.path = "update.ota"
	a.firmware.uploading = true
	a.firmware.percent = new(atomic.Int32)
	a.firmware.percent.Store(42)
	var ops op.Ops
	var router input.Router
	if size := a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight)); size.Size.X == 0 {
		t.Fatal("the page drew nothing")
	}
}

func TestLeavingAGarlandDropsTheCommandsWaitingForIt(t *testing.T) {
	a := sceneApp(t, PageEffects)
	a.client = &api.Client{}
	release := make(chan struct{})
	var second atomic.Bool
	a.work(func(context.Context, *api.Client) (func(), error) {
		<-release
		return nil, nil
	})
	a.work(func(context.Context, *api.Client) (func(), error) {
		second.Store(true)
		return nil, nil
	})
	a.client = nil
	a.disconnect()
	close(release)
	time.Sleep(100 * time.Millisecond)
	if second.Load() {
		t.Fatal("a command went to the garland after it was left")
	}
	// A new connection is not the old one's.
	a.client = &api.Client{}
	ran := make(chan struct{})
	a.work(func(context.Context, *api.Client) (func(), error) {
		close(ran)
		return nil, nil
	})
	select {
	case <-ran:
	case <-time.After(5 * time.Second):
		t.Fatal("the next garland's command never ran")
	}
}

func TestDisconnectLeavesForTheListAndStaysPaired(t *testing.T) {
	a := sceneApp(t, PageEffects)
	a.journalOpen = true
	a.leaveGarland()
	if a.connected || a.client != nil || a.journalOpen || a.connect.stage != stageAddress {
		t.Fatalf("connected %v, journal %v, stage %v", a.connected, a.journalOpen, a.connect.stage)
	}
}

func TestAFormBeingSavedHoldsStill(t *testing.T) {
	a := sceneApp(t, PageSchedule)
	a.schedule.openForm(0)
	a.schedule.saving = true
	a.schedule.form.cancel.Click()
	a.schedule.add.Click()
	var ops op.Ops
	var router input.Router
	a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight))
	if a.schedule.editing != 0 {
		t.Fatalf("editing %d: the form closed under its own answer", a.schedule.editing)
	}
}

func TestOnlyTheLastEffectAskedForOpens(t *testing.T) {
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`{"from":"` + r.URL.Query().Get("id") + `"}`))
	})
	a := sceneApp(t, PageEffects)
	a.client = client
	a.online = stateOnline
	a.tune.open(a, "first")
	a.tune.open(a, "second")
	settleWork(t, a)
	if a.modal != modalTune || a.tune.id.Text() != "second" || !strings.Contains(a.tune.file.Text(), "second") {
		t.Fatalf("modal %v, id %q, file %q", a.modal, a.tune.id.Text(), a.tune.file.Text())
	}
	// A draft in a window opened meanwhile stays.
	a.tune.open(a, "third")
	a.tune.take("mine", []byte(`{"from":"disk"}`))
	a.openModal(modalTune)
	a.tune.file.SetText(`{"from":"typed"}`)
	settleWork(t, a)
	if a.tune.id.Text() != "mine" || a.tune.file.Text() != `{"from":"typed"}` {
		t.Fatalf("the draft was replaced: %q %q", a.tune.id.Text(), a.tune.file.Text())
	}
}

func TestSettingTheClockFromTheInternetTakesItsTurn(t *testing.T) {
	var mutex sync.Mutex
	var bodies []string
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		switch {
		case strings.HasSuffix(r.URL.Path, "/time/rtc"):
			var body map[string]any
			if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
				t.Errorf("body: %v", err)
			}
			source, _ := body["source"].(string)
			mutex.Lock()
			bodies = append(bodies, source)
			mutex.Unlock()
			answer(t, w, api.Clock{})
		case strings.HasSuffix(r.URL.Path, "/schedule"):
			answer(t, w, sceneSchedule())
		default:
			answer(t, w, api.Clock{})
		}
	})
	a := sceneApp(t, PageSchedule)
	a.client = client
	release := make(chan struct{})
	a.work(func(context.Context, *api.Client) (func(), error) {
		<-release
		return nil, nil
	})
	a.schedule.setModule(a, func(ctx context.Context, client *api.Client) (api.Clock, error) {
		return client.SetModuleByHand(ctx, "2026-12-25T18:00")
	})
	a.schedule.setModuleFromInternet(a)
	time.Sleep(50 * time.Millisecond)
	close(release)
	deadline := time.Now().Add(10 * time.Second)
	for time.Now().Before(deadline) {
		mutex.Lock()
		count := len(bodies)
		mutex.Unlock()
		if count == 2 {
			break
		}
		time.Sleep(time.Millisecond)
	}
	mutex.Lock()
	defer mutex.Unlock()
	if strings.Join(bodies, " ") != "manual ntp" {
		t.Fatalf("%v", bodies)
	}
}

func TestAnOldInternetSourceIsNoSuccessOfThisAsking(t *testing.T) {
	ntp := api.Clock{Synced: true}
	ntp.Module.Trusted = true
	ntp.Module.Source = ntpSource
	manual := api.Clock{Synced: true}
	manual.Module.Trusted = true
	manual.Module.Source = "manual"
	if words := InternetClockText(ntp, ntp); strings.Contains(words, "Clock module set") {
		t.Fatal(words)
	}
	if words := InternetClockText(manual, ntp); words != "Clock module set from the internet." {
		t.Fatal(words)
	}
	if words := InternetClockText(manual, manual); !strings.Contains(words, "has not yet") {
		t.Fatal(words)
	}
}

func TestAPlayThatFailedPutsTheMarkBack(t *testing.T) {
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "busy", http.StatusConflict)
	})
	a := sceneApp(t, PageEffects)
	a.client = client
	a.effects.play(a, "comet")
	if !a.effects.find("comet").Active {
		t.Fatal("no mark at once")
	}
	settleWork(t, a)
	if a.effects.find("comet").Active || !a.effects.find(sceneEffect).Active || a.effects.commands != 0 {
		t.Fatalf("comet %v, %s %v, commands %d", a.effects.find("comet").Active, sceneEffect, a.effects.find(sceneEffect).Active, a.effects.commands)
	}
	if !a.failed {
		t.Fatal("the failure was not said")
	}
}

func TestAPlayThatWorkedIsNotCalledFailedForItsReadingBack(t *testing.T) {
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		if r.Method == http.MethodPost {
			answer(t, w, map[string]bool{"ok": true})
			return
		}
		http.Error(w, "busy", http.StatusServiceUnavailable)
	})
	a := sceneApp(t, PageEffects)
	a.client = client
	a.effects.play(a, "comet")
	deadline := time.Now().Add(5 * time.Second)
	for a.effects.commands != 0 && time.Now().Before(deadline) {
		a.drain()
		time.Sleep(time.Millisecond)
	}
	if a.effects.commands != 0 || a.failed || !a.effects.find("comet").Active {
		t.Fatalf("commands %d, %q, comet %v", a.effects.commands, a.status, a.effects.find("comet").Active)
	}
}

func TestAStateReadWhileACommandGoesDoesNotTakeItBack(t *testing.T) {
	a := sceneApp(t, PageEffects)
	a.effects.commands = 1
	before := a.state
	asked := api.State{Effect: "comet"}
	if asked.Effect == before.Effect {
		t.Fatal("the same state says nothing")
	}
	a.client = stubGarland(t, func(w http.ResponseWriter, r *http.Request) { answer(t, w, asked) })
	a.about.Version = "known"
	a.refreshState()
	deadline := time.Now().Add(5 * time.Second)
	for a.stateAsked && time.Now().Before(deadline) {
		a.drain()
		time.Sleep(time.Millisecond)
	}
	if a.stateAsked || a.state.Effect != before.Effect {
		t.Fatalf("asked %v, effect %q", a.stateAsked, a.state.Effect)
	}
	// And a state that says what is already known still puts the marks right.
	a.effects.commands = 0
	a.effects.markActive("comet")
	a.takeState(before)
	if a.effects.find("comet").Active || !a.effects.find(sceneEffect).Active {
		t.Fatal("the marks stayed wrong")
	}
}

func TestANameFromTheListDoesNotStayForATypedAddress(t *testing.T) {
	a := sceneConnect(t)
	a.connect.name = "attic.local"
	a.connect.start(a)
	if a.connect.name != "" {
		t.Fatal(a.connect.name)
	}
}

func TestLeavingForEffectsEndsAReachForAGarland(t *testing.T) {
	a := sceneConnect(t)
	a.connect.busy = true
	before := a.connect.pairing.Load()
	a.enterLocal()
	if a.connect.busy || a.connect.pairing.Load() == before {
		t.Fatalf("busy %v", a.connect.busy)
	}
}

func TestARowGoesOnToANewLineWhenItNoLongerFits(t *testing.T) {
	var ops op.Ops
	var router input.Router
	gtx := sceneContext(&ops, &router, 100, 200)
	gtx.Constraints.Min = image.Point{}
	item := func(gtx C) D { return D{Size: image.Pt(40, 20)} }
	if size := row(gtx, item, item); size.Size != image.Pt(88, 20) {
		t.Fatalf("%v", size.Size)
	}
	if size := row(gtx, item, item, item); size.Size != image.Pt(88, 48) {
		t.Fatalf("%v", size.Size)
	}
}

func TestEveryButtonOutsideARowHasWordsToShow(t *testing.T) {
	a := sceneApp(t, PageFirmware)
	var ops op.Ops
	var router input.Router
	gtx := sceneContext(&ops, &router, sceneWidth, sceneHeight)
	gtx.Constraints.Min = image.Point{}
	icon := a.iconButton(gtx, &a.rescan, buttonOther, "refresh", "Refresh", true)
	labelled := a.labelButton(gtx, &a.rescan, buttonOther, "refresh", "Refresh", "", true)
	if labelled.Size.X <= icon.Size.X || labelled.Size.Y != icon.Size.Y {
		t.Fatalf("with its words %v, a glyph alone %v", labelled.Size, icon.Size)
	}
}

// Nothing of a failure is reported for a command dropped along with its garland.
func TestACancelledCommandSaysNothing(t *testing.T) {
	a := sceneApp(t, PageEffects)
	a.settle(context.Canceled, true)
	if a.status != "" {
		t.Fatalf("%q", a.status)
	}
}

// Anyone can answer a search, so a garland met before is not taken to have moved until it answers
// at the new address under its own root. Something there without it is left, with no offer to trust it.
func TestAnAddressFromTheSearchIsKeptOnlyOnceTheGarlandAnswersThere(t *testing.T) {
	server := httptest.NewTLSServer(http.NotFoundHandler())
	t.Cleanup(server.Close)
	rootPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: server.Certificate().Raw})
	for _, answered := range []error{nil, fmt.Errorf("reach the garland: %w", api.ErrWrongRoot)} {
		a := newApp("1.0.0")
		t.Cleanup(a.stop)
		a.settingsPath = filepath.Join(t.TempDir(), "utility.json")
		kept := settings.Garland{Name: "garland", Address: "192.0.2.50", RootPEM: string(rootPEM), Fingerprint: "AA"}
		a.settings.Remember(kept)
		if err := a.connectKnown(kept, "192.0.2.77", "a-token"); err != nil {
			t.Fatal(err)
		}
		if stored, _ := a.settings.Find("AA"); stored.Address != kept.Address || !a.unproven || a.current.Address != "192.0.2.77" {
			t.Fatalf("kept %q before an answer, unproven %v", stored.Address, a.unproven)
		}
		a.settle(answered, false)
		loaded, err := settings.Load(a.settingsPath)
		if err != nil {
			t.Fatal(err)
		}
		stored, _ := loaded.Find("AA")
		switch {
		case answered == nil && (stored.Address != "192.0.2.77" || a.unproven || !a.connected):
			t.Fatalf("answered there: kept %q, unproven %v, connected %v", stored.Address, a.unproven, a.connected)
		case answered != nil && (stored.Address != kept.Address || a.connected || a.connect.stage != stageAddress || !strings.Contains(a.status, "without its root")):
			t.Fatalf("a stranger there: kept %q, connected %v, stage %v, %q", stored.Address, a.connected, a.connect.stage, a.status)
		}
	}
}

// A clock wrong here or on the garland is said, even of a look nobody asked for, and the garland stays.
func TestAClockWrongSomewhereIsSaidAndTheGarlandKept(t *testing.T) {
	a := sceneApp(t, PageEffects)
	err := fmt.Errorf("reach the garland: %w", api.ErrCertificateDates)
	a.settle(err, false)
	if !a.connected || !a.failed || !strings.Contains(a.status, "clock") {
		t.Fatalf("connected %v, failed %v, %q", a.connected, a.failed, a.status)
	}
	if !installWasRefused(err) || strings.Contains(mayHaveTaken(err).Error(), "may have taken") {
		t.Fatal("a handshake that failed was taken as maybe having sent something")
	}
}

// A row's Delete pressed once does not wait to take whatever action comes to its place.
func TestADeletePressedOnceIsNotLeftForAnotherAction(t *testing.T) {
	var page schedulePage
	page.init()
	page.take(sceneSchedule())
	page.rowsOf[0].remove.press(time.Now())
	changed := sceneSchedule()
	changed.Revision++
	changed.Entries = changed.Entries[1:]
	page.take(changed)
	if page.rowsOf[0].remove.press(time.Now()) {
		t.Fatal("one press deleted an action")
	}
}

// Every speed and zone has its button.
func TestEveryChoiceHasItsButton(t *testing.T) {
	var page schedulePage
	if len(speedSegments) != len(ScheduleSpeeds) || len(page.zones) != len(Zones) || len(daySegments) != len(page.form.days) {
		t.Fatalf("%d segments for %d speeds, %d buttons for %d zones, %d for %d days", len(speedSegments), len(ScheduleSpeeds), len(page.zones), len(Zones), len(daySegments), len(page.form.days))
	}
}
