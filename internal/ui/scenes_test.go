// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: A garland's worth of made-up answers for the pages, and every page laid out with them.
// Contributors: None

package ui

import (
	"context"
	"encoding/json"
	"encoding/pem"
	"errors"
	"fmt"
	"image"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"gioui.org/f32"
	"gioui.org/io/input"
	"gioui.org/io/pointer"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/unit"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/settings"
)

const (
	sceneWidth  = 1040
	sceneHeight = 960
	sceneEffect = "aurora"
)

func sceneEffects() []api.Effect {
	return []api.Effect{
		{ID: "chase-rainbow", Name: "Chase Rainbow", Group: "Chases", Favorite: true},
		{ID: "comet", Name: "Comet", Group: "Chases"},
		{ID: sceneEffect, Name: "Aurora", Group: "Nature", Favorite: true, Active: true},
		{ID: "fireflies", Name: "Fireflies", Group: "Nature", Favorite: true},
		{ID: "snowfall", Name: "Snowfall", Group: "Nature"},
		{ID: "candy-cane", Name: "Candy Cane", Group: "Holiday", Favorite: true},
		{ID: "my-sparkle", Name: "My Sparkle", Group: UploadedGroup, Uploaded: true, Favorite: true},
		{ID: "theater", Name: "Theater, tuned", Group: UploadedGroup, Uploaded: true, BuiltIn: true},
	}
}

func sceneState() api.State {
	return api.State{
		Power: true, Brightness: 60, Effect: sceneEffect, EffectName: "Aurora", Speed: 1.25,
		Leds: api.Leds{Count: 32, Max: 256, Probe: "present", Estimate: 32},
	}
}

func sceneNext() api.NextAction {
	return api.NextAction{Clock: true, At: "Today 18:00", Power: api.StepOn, EffectName: "Aurora"}
}

func sceneSchedule() api.Schedule {
	return api.Schedule{
		Max: 16, Revision: 3, Next: sceneNext(),
		Entries: []api.Action{
			{Enabled: true, Time: "18:00", Days: allDays, Power: api.StepOn, Brightness: 60, Effect: sceneEffect},
			{Enabled: true, Time: "23:30", Days: allDays, Power: api.StepOff},
			{Enabled: false, Time: "07:15", Days: 0b0111110, Power: api.StepOn, Speed: 0.5},
		},
	}
}

// The next 24 hours of sceneSchedule, from a moment taken so that the bar is drawn the same each time.
func sceneTimeline(now int64) *api.Timeline {
	return &api.Timeline{Synced: true, Events: []api.TimelineEvent{
		{At: now + 3*3600, Time: "18:00", Day: 4, Entries: []int{0, 2}, Power: api.StepOn, Brightness: 60, Effect: sceneEffect},
		{At: now + 8*3600 + 1800, Time: "23:30", Day: 4, Entries: []int{1}, Power: api.StepOff},
	}}
}

func scenePlaylist() api.Playlist {
	return api.Playlist{Effects: []string{sceneEffect, "fireflies", "candy-cane"}, Dwell: 90, CrossFade: 1000, Max: 16}
}

func sceneInfo() api.Info {
	return api.Info{
		Model: "ESP32-C6-LCD-1.3", Chip: "ESP32-C6 rev 0.2", MAC: "00:00:5E:00:53:01",
		RAMTotal: 450 << 10, RAMFree: 196 << 10, RAMLowest: 158 << 10, FlashTotal: 4096 << 10,
		FirmwareSlot: 1856 << 10, FirmwareUsed: 1312 << 10,
		IP: "192.0.2.50", Hostname: "csigarland.local", HTTPS: true, LedOutput: true,
		RootFingerprint:  "3F:A1:9C:07:52:EE:1B:64:D0:88:2A:71:C4:39:FE:06",
		CertificateUntil: time.Date(2036, 11, 2, 0, 0, 0, 0, time.UTC).Unix(),
		Effects:          154, Favorites: 12, Power: true, Next: sceneNext(),
	}
}

func sceneClients() []api.PairedClient {
	until := time.Date(2027, 2, 10, 0, 0, 0, 0, time.UTC).Unix()
	return []api.PairedClient{
		{ID: 1, Label: "csiGarland utility, WORKSHOP", Expires: until, This: true},
		{ID: 2, Label: "Phone browser", Expires: until},
	}
}

func sceneLog() string {
	return "I (1204) wifi: joined home network\nI (1530) portal: listening on 443\n" +
		"I (2210) ntp: time synced\nI (60012) ota: image confirmed\nI (91234) lighting: effect aurora\n"
}

// sceneApp is the window as it stands on a page, after the garland has answered.
func sceneApp(t *testing.T, page Page) *App {
	t.Helper()
	a := newApp("1.0.0")
	t.Cleanup(a.stop)
	a.connected = true
	a.online = stateOnline
	a.current.Address = "192.0.2.50"
	a.about = api.About{Version: "1.0.0.90"}
	a.state = sceneState()
	a.page = page
	switch page {
	case PageEffects:
		a.effects.list = sceneEffects()
		a.effects.playlist.take(scenePlaylist())
	case PageSchedule:
		a.schedule.take(sceneSchedule())
		a.schedule.timeline = sceneTimeline(time.Now().Unix())
		a.schedule.effects = sceneEffects()
		for _, effect := range a.schedule.effects {
			a.schedule.names[effect.ID] = effect.Name
		}
		a.schedule.clock = api.Clock{Synced: true, Now: "2026-11-02 15:04", Zone: "CET-1CEST,M3.5.0,M10.5.0/3",
			Module: api.ClockModule{Present: true, Trusted: true, Now: "2026-11-02 15:04:00", Source: "ntp"}}
	case PageDevice:
		a.device.mode.Value = "auto"
		a.device.count.SetText("32")
		a.device.takeDisplay(api.Display{Saver: "starfield", Delay: 60, ScreenBrightness: 100, SaverBrightness: 30})
	case PageNetwork:
		a.network.wifi = api.Wifi{Configured: true, Connected: true, SSID: "Home", IP: "192.0.2.50", Hostname: "csigarland.local"}
		a.network.rootText = sceneInfo().RootFingerprint
		a.network.ssid.SetText("Home")
		a.network.name.SetText("csigarland")
		a.network.found = []api.Network{{SSID: "Home", RSSI: -48}, {SSID: "Guests", RSSI: -71, Open: true}}
	case PageFirmware:
		a.firmware.running = api.Firmware{Version: "1.0.0.90", Effects: 154}
	case PageInfo:
		a.info.info = sceneInfo()
		a.info.clients = sceneClients()
	case PageLog:
		a.logPage.lines.Add(sceneLog(), 0)
	}
	return a
}

func sceneContext(ops *op.Ops, router *input.Router, width, height int) C {
	return layout.Context{
		Ops:         ops,
		Metric:      unit.Metric{PxPerDp: 1, PxPerSp: 1},
		Now:         time.Now(),
		Constraints: layout.Exact(image.Pt(width, height)),
		Source:      router.Source(),
	}
}

func TestEveryPageLaysOut(t *testing.T) {
	for page := PageEffects; page < pageCount; page++ {
		t.Run(pageNames[page], func(t *testing.T) {
			a := sceneApp(t, page)
			var ops op.Ops
			var router input.Router
			// Twice: what a first frame opens, a second one lays out.
			for range 2 {
				ops.Reset()
				if size := a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight)); size.Size.X == 0 {
					t.Fatal("the page drew nothing")
				}
				router.Frame(&ops)
			}
		})
	}
}

func sceneTune(t *testing.T) *App {
	t.Helper()
	a := sceneApp(t, PageEffects)
	file, known := libraryFile(sceneEffect)
	if !known {
		t.Fatalf("no effect %s", sceneEffect)
	}
	a.tune.take(sceneEffect, file)
	a.openModal(modalTune)
	a.tune.look.clock.played = 3 * time.Second
	return a
}

// Tune scrolled to its colours, with the picker open on the palette's second stop.
func sceneTuneColours(t *testing.T) *App {
	t.Helper()
	a := sceneTune(t)
	if len(a.tune.fileColors) < 2 {
		t.Fatalf("%s has %d colours", sceneEffect, len(a.tune.fileColors))
	}
	a.tune.colors.choose(a.tune.fileColors[1])
	a.tune.controls.Position = layout.Position{First: 2, Offset: 230}
	return a
}

func scenePreview(t *testing.T) *App {
	t.Helper()
	a := sceneApp(t, PageEffects)
	a.preview.open(a, sceneEffect, "Aurora")
	if a.modal != modalPreview {
		t.Fatalf("no preview: %s", a.status)
	}
	return a
}

func sceneLocal(t *testing.T) *App {
	t.Helper()
	a := newApp("1.0.0.1")
	t.Cleanup(a.stop)
	a.enterLocal()
	return a
}

func sceneConnect(t *testing.T) *App {
	t.Helper()
	a := newApp("1.0.0.1")
	t.Cleanup(a.stop)
	a.connect.looked = true
	a.settings.Garlands = []settings.Garland{
		{Name: "csigarland.local", Address: "192.0.2.50", Fingerprint: "AA", LastUsed: 3},
		{Name: "workshop.local", Address: "192.0.2.77", Fingerprint: "BB", LastUsed: 2},
	}
	a.connect.paired = map[string]bool{"AA": true, "BB": true}
	a.connect.found = []api.Found{
		{Name: "csigarland.local", Address: "192.0.2.50", Version: "1.0.0.96", Fingerprint: "AA"},
		{Name: "attic.local", Address: "192.0.2.61", Version: "1.0.0.96", Fingerprint: "CC"},
	}
	return a
}

func TestTheWindowsThatOpenOverAPageLayOut(t *testing.T) {
	for name, a := range map[string]*App{"tune": sceneTune(t), "tune colours": sceneTuneColours(t), "preview": scenePreview(t), "local": sceneLocal(t), "connect": sceneConnect(t)} {
		var ops op.Ops
		var router input.Router
		for range 2 {
			ops.Reset()
			if size := a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight)); size.Size.X == 0 {
				t.Fatalf("%s drew nothing", name)
			}
			router.Frame(&ops)
		}
	}
}

func TestATipShowsOnlyAfterAMoment(t *testing.T) {
	a := sceneApp(t, PageEffects)
	var ops op.Ops
	var router input.Router
	gtx := sceneContext(&ops, &router, sceneWidth, sceneHeight)
	a.tipOffered = "Play"
	a.layoutTip(gtx)
	if a.tipWords != "Play" || a.tipSince != gtx.Now {
		t.Fatalf("%q since %v", a.tipWords, a.tipSince)
	}
	// In the window's far corner, which has it keep inside.
	a.tipOffered = "Play"
	a.pointerAt = f32.Pt(sceneWidth, sceneHeight)
	gtx.Now = gtx.Now.Add(tipDelay)
	a.layoutTip(gtx)
	a.layoutTip(gtx)
	if a.tipWords != "" {
		t.Fatalf("a tip stayed with nothing offering it: %q", a.tipWords)
	}
}

// A button's tip follows the pointer as its own hover sees it: not from under a window, where nothing
// of the page reads events, and not after the pointer has left a button drawn disabled.
func TestATipIsOnlyForWhatThePointerIsOverNow(t *testing.T) {
	a := sceneApp(t, PageEffects)
	var ops op.Ops
	var router input.Router
	click := new(widget.Clickable)
	draw := func(under, enabled bool) string {
		t.Helper()
		ops.Reset()
		a.frames++
		a.tipOffered = ""
		gtx := sceneContext(&ops, &router, sceneWidth, sceneHeight)
		a.source = gtx.Source
		if under {
			gtx = gtx.Disabled()
		}
		a.labelButton(gtx, click, buttonOther, "", "Words", "The tip", enabled)
		router.Frame(&ops)
		return a.tipOffered
	}
	move := func(x, y float32) {
		router.Queue(pointer.Event{Kind: pointer.Move, Source: pointer.Mouse, Position: f32.Pt(x, y)})
	}
	draw(false, true)
	move(5, 5)
	if tip := draw(false, true); tip != "The tip" {
		t.Fatalf("over the button: %q", tip)
	}
	if tip := draw(true, true); tip != "" {
		t.Fatalf("under a window: %q", tip)
	}
	if tip := draw(false, false); tip != "The tip" {
		t.Fatalf("over the button drawn disabled: %q", tip)
	}
	move(600, 600)
	if tip := draw(false, false); tip != "" {
		t.Fatalf("after leaving the button drawn disabled: %q", tip)
	}
	// Pressed, then a window over it, the pointer gone elsewhere, and the window closed.
	move(5, 5)
	draw(false, true)
	draw(true, true)
	move(600, 600)
	draw(true, true)
	if tip := draw(false, true); tip != "" {
		t.Fatalf("left behind by a window since closed: %q", tip)
	}
}

// The pointer on the dimmed page around a window is over the window, not over a button under it.
func TestAWindowTakesThePointerFromItsPage(t *testing.T) {
	a := sceneApp(t, PageEffects)
	var ops op.Ops
	var router input.Router
	click := new(widget.Clickable)
	draw := func() {
		ops.Reset()
		a.frames++
		gtx := sceneContext(&ops, &router, sceneWidth, sceneHeight)
		a.source = gtx.Source
		a.labelButton(gtx.Disabled(), click, buttonOther, "", "Words", "", true)
		a.modal = modalPreview
		a.layoutModal(gtx)
		router.Frame(&ops)
	}
	draw()
	router.Queue(pointer.Event{Kind: pointer.Move, Source: pointer.Mouse, Position: f32.Pt(5, 5)})
	draw()
	draw()
	if a.hovers[click].hover.Update(a.source) {
		t.Fatal("a button under the window took the pointer")
	}
}

func TestCommandsGoInTheOrderAskedFor(t *testing.T) {
	a := sceneApp(t, PageEffects)
	a.client = &api.Client{}
	release := make(chan struct{})
	var mutex sync.Mutex
	var order []string
	mark := func(name string) {
		mutex.Lock()
		order = append(order, name)
		mutex.Unlock()
	}
	finished := make(chan struct{})
	a.work(func(context.Context, *api.Client) (func(), error) {
		<-release
		mark("first")
		return nil, nil
	})
	a.work(func(context.Context, *api.Client) (func(), error) {
		mark("second")
		close(finished)
		return nil, nil
	})
	time.Sleep(50 * time.Millisecond)
	mutex.Lock()
	early := len(order)
	mutex.Unlock()
	if early != 0 {
		t.Fatal("the second went ahead of the first")
	}
	close(release)
	<-finished
	if strings.Join(order, " ") != "first second" {
		t.Fatalf("%v", order)
	}
}

func TestAnAddressSeenAgainWithoutItsRootIsStillTheSameGarland(t *testing.T) {
	known := []settings.Garland{{Name: "last", Address: "192.0.2.1", Fingerprint: "AA"}}
	rows := GarlandRows(known, func(string) bool { return true }, []api.Found{{Name: "x", Address: "192.0.2.1", Version: "2"}})
	if len(rows) != 1 || !rows[0].Answering || rows[0].Version != "2" {
		t.Fatalf("%+v", rows)
	}
}

func TestOnlyWhatIsShownOnTheScreenFormIsRead(t *testing.T) {
	var page devicePage
	page.init()
	page.takeDisplay(api.Display{Saver: "starfield", Delay: 60, ScreenBrightness: 100, SaverBrightness: 30})
	page.delay.SetText("nonsense")
	page.saverLevel.SetText("nonsense")
	page.saver.Value = "none"
	wanted, err := page.readDisplay()
	if err != nil || wanted.Delay != 60 || wanted.SaverBrightness != 30 || wanted.Saver != "none" {
		t.Fatalf("%+v, %v", wanted, err)
	}
	page.saver.Value = "black-screen"
	if _, err := page.readDisplay(); err == nil {
		t.Fatal("a delay on show that is no number was taken")
	}
	page.delay.SetText("120")
	wanted, err = page.readDisplay()
	if err != nil || wanted.Delay != 120 || wanted.SaverBrightness != 30 {
		t.Fatalf("%+v, %v", wanted, err)
	}
}

func TestWithTheLibraryShowingAFileComesFromItAtOnce(t *testing.T) {
	a := sceneApp(t, PageEffects)
	a.client = &api.Client{}
	a.connected = true
	a.effects.list = nil
	a.online = stateOffline
	got := false
	a.withEffectFile(sceneEffect, func(file []byte) { got = len(file) > 0 })
	if !got {
		t.Fatal("it went to ask a garland that does not answer")
	}
}

func TestTheMenuAlwaysLeadsSomewhere(t *testing.T) {
	a := newApp("1.0.0.1")
	t.Cleanup(a.stop)
	// On the connect page, with no garland: the journal, and back out of it by the menu.
	a.journalOpen = true
	a.choosePage(PageSchedule)
	if !a.journalOpen || a.local {
		t.Fatal("a page that needs a garland opened with none")
	}
	a.choosePage(PageEffects)
	if a.journalOpen || !a.local || !a.pageShown() {
		t.Fatalf("journal %v, local %v", a.journalOpen, a.local)
	}
	// Working with no garland: the journal, and back to the effects without starting over.
	a.effects.opened["kept"] = &groupState{open: true}
	a.journalOpen = true
	a.choosePage(PageEffects)
	if a.journalOpen || a.effects.opened["kept"] == nil {
		t.Fatal("going back to the effects lost them, or stayed in the journal")
	}
	// Connected: every page.
	b := sceneApp(t, PageEffects)
	b.journalOpen = true
	b.choosePage(PageLog)
	if b.page != PageLog || b.journalOpen {
		t.Fatalf("page %v, journal %v", b.page, b.journalOpen)
	}
}

func TestEveryGlyphCanBeDrawn(t *testing.T) {
	for name, path := range glyphs {
		if _, err := parseGlyph(path); err != nil {
			t.Errorf("%s: %v", name, err)
		}
	}
	for _, bad := range []string{"M1", "X1 2", "M1 two"} {
		if _, err := parseGlyph(bad); err == nil {
			t.Errorf("%s: taken", bad)
		}
	}
}

func TestThePortalsGlyphsAreTheseOnes(t *testing.T) {
	assets, err := os.ReadFile(filepath.Join("..", "..", "firmware", "app", "src", "portal_assets.h"))
	if err != nil {
		t.Fatal(err)
	}
	table := regexp.MustCompile(`const GLYPHS = \{(.*)\};`).FindSubmatch(assets)
	if table == nil {
		t.Fatal("no table in the portal")
	}
	entries := regexp.MustCompile(`(\w+):'([^']+)'`).FindAllSubmatch(table[1], -1)
	if len(entries) == 0 {
		t.Fatal("an empty table")
	}
	for _, entry := range entries {
		if glyphs[string(entry[1])] != string(entry[2]) {
			t.Errorf("%s differs", entry[1])
		}
	}
}

func TestAClosingWindowAsksIfItWouldLoseSomething(t *testing.T) {
	a := sceneTune(t)
	now := time.Now()
	a.closeModal(now)
	if a.modal != modalNone {
		t.Fatal("nothing to lose, and it stayed")
	}
	a.tune.take(sceneEffect, []byte(`{}`))
	a.openModal(modalTune)
	a.tune.file.SetText(`{"changed":1}`)
	a.closeModal(now)
	if a.modal != modalTune {
		t.Fatal("it closed on what was not saved")
	}
	a.closeModal(now.Add(time.Second))
	if a.modal != modalNone {
		t.Fatal("a second press did not close it")
	}
}

func TestWithNoGarlandTheBuiltInEffectsAreThere(t *testing.T) {
	a := sceneLocal(t)
	if !a.effects.showsLibrary(a) || len(libraryEffects()) == 0 {
		t.Fatal("no effects to work on")
	}
	done := false
	a.withEffectFile(sceneEffect, func(file []byte) { done = len(file) > 0 })
	if !done {
		t.Fatal("no file for a built-in effect")
	}
	a.leaveLocal()
	if a.local {
		t.Fatal("still working with no garland")
	}
}

func TestTheListPutsWhoWasMetFirstAndKnowsThemByTheirRoot(t *testing.T) {
	known := []settings.Garland{
		{Name: "last", Address: "192.0.2.1", Fingerprint: "AA", LastUsed: 9},
		{Name: "before", Address: "192.0.2.2", Fingerprint: "BB", LastUsed: 5},
	}
	found := []api.Found{
		{Name: "new", Address: "192.0.2.3", Version: "3", Fingerprint: "CC"},
		{Name: "moved", Address: "192.0.2.4", Version: "2", Fingerprint: "BB"},
		{Name: "silent", Address: "192.0.2.5", Version: "1"},
	}
	rows := GarlandRows(known, func(fingerprint string) bool { return fingerprint == "AA" }, found)
	if len(rows) != 4 || rows[0].Name != "last" || rows[1].Name != "before" || rows[2].Name != "new" || rows[3].Name != "silent" {
		t.Fatalf("%+v", rows)
	}
	if !rows[0].Paired || rows[0].Answering || rows[1].Paired || !rows[1].Answering {
		t.Fatalf("%+v", rows)
	}
	if rows[1].Address != "192.0.2.4" || rows[1].Version != "2" {
		t.Fatalf("it moved, and the list did not say so: %+v", rows[1])
	}
	if rows[0].Text() != "192.0.2.1, paired, not answering" || rows[1].Text() != "192.0.2.4, firmware 2" {
		t.Fatalf("%q %q", rows[0].Text(), rows[1].Text())
	}
}

// The rail, footer and journal hold what they are given.
func TestChrome(t *testing.T) {
	a := sceneApp(t, PageEffects)
	a.reportError(errors.New("first"))
	a.report("second")
	if a.status != "second" || a.failed || len(a.journal) != 2 || a.journalUnseen != 1 {
		t.Fatalf("status %q failed %v, %d in the journal, %d unseen", a.status, a.failed, len(a.journal), a.journalUnseen)
	}
	a.report("")
	if len(a.journal) != 2 {
		t.Fatal("an empty line went in the journal")
	}
	// The same line again is the one there, brought up to now.
	for range 3 {
		a.reportError(errors.New("second"))
	}
	if len(a.journal) != 3 || a.journalUnseen != 2 {
		t.Fatalf("%d in the journal, %d unseen", len(a.journal), a.journalUnseen)
	}
	for i := range journalKept + 5 {
		a.report(fmt.Sprintf("again %d", i))
	}
	if len(a.journal) != journalKept {
		t.Fatalf("the journal kept %d", len(a.journal))
	}
	a.journalOpen = true
	var ops op.Ops
	var router input.Router
	if size := a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight)); size.Size.X == 0 {
		t.Fatal("the journal drew nothing")
	}
	a.noteAnswer(errors.New("no route to host"))
	if a.online != stateOffline {
		t.Fatal("a failed connection is not offline")
	}
	a.noteAnswer(&api.Error{Status: http.StatusConflict})
	if a.online != stateOnline {
		t.Fatal("a refusal is still an answer")
	}
}

func TestAFormDoesNotOutliveTheListItWasOpenedOn(t *testing.T) {
	a := sceneApp(t, PageSchedule)
	a.schedule.openForm(2)
	a.schedule.take(sceneSchedule())
	if a.schedule.editing != 2 {
		t.Fatal("reading the same list again closed the form")
	}
	shorter := sceneSchedule()
	shorter.Revision++
	shorter.Entries = shorter.Entries[:1]
	a.schedule.take(shorter)
	if a.schedule.editing != noAction {
		t.Fatalf("still editing %d of a list of %d", a.schedule.editing, len(shorter.Entries))
	}
	a.schedule.openForm(newAction)
	shorter.Revision++
	a.schedule.take(shorter)
	if a.schedule.editing != newAction {
		t.Fatal("a new action lost its form, and it has no place to lose")
	}
}

func TestNothingOfOneGarlandIsLeftForTheNext(t *testing.T) {
	a := sceneApp(t, PageNetwork)
	a.network.take(api.Wifi{Configured: true, SSID: "Home", Hostname: "old.local"}, api.Info{Domain: "old.example.com"})
	a.logPage.take(api.LogPart{Text: "line\n", Position: 5, Boot: "a"})
	a.effects.list = sceneEffects()
	a.stateAsked = true
	a.disconnect()
	if a.network.name.Text() != "" || a.network.domain.Text() != "" || a.network.ssid.Text() != "" {
		t.Fatal("the last garland's name or network would go to the next")
	}
	if a.logPage.boot != "" || a.logPage.lines.Count() != 0 || len(a.effects.list) != 0 || a.about.Version != "" || a.stateAsked {
		t.Fatal("something of the last garland stayed")
	}
}

func TestWhatAGarlandSaysReplacesAStaleName(t *testing.T) {
	var page networkPage
	page.init()
	page.name.SetText("old")
	page.domain.SetText("old.example.com")
	page.take(api.Wifi{Configured: true, SSID: "Home", Hostname: "new.local"}, api.Info{Domain: ""})
	if page.name.Text() != "new" || page.domain.Text() != "" || page.ssid.Text() != "Home" {
		t.Fatalf("%q %q %q", page.name.Text(), page.domain.Text(), page.ssid.Text())
	}
}

func TestAnAnswerFromAGarlandSinceLeftIsDropped(t *testing.T) {
	a := sceneApp(t, PageEffects)
	old := &api.Client{}
	a.client = old
	release := make(chan struct{})
	ran := false
	a.work(func(context.Context, *api.Client) (func(), error) {
		<-release
		return func() { ran = true }, api.ErrWrongRoot
	})
	current := &api.Client{}
	a.client = current
	close(release)
	deadline := time.Now().Add(200 * time.Millisecond)
	for time.Now().Before(deadline) {
		a.drain()
		time.Sleep(time.Millisecond)
	}
	if ran || !a.connected || a.client != current {
		t.Fatalf("ran %v, connected %v", ran, a.connected)
	}
}

func TestALineNotFinishedIsShownAndCopied(t *testing.T) {
	var lines LogLines
	lines.Add("I (1) one\nI (2) unfin", 0)
	if lines.Count() != 2 || lines.Line(1) != "I (2) unfin" || !strings.HasSuffix(lines.Text(), "I (2) unfin") {
		t.Fatalf("%d %q", lines.Count(), lines.Text())
	}
	lines.Add("ished\n", 0)
	if lines.Count() != 2 || lines.Line(1) != "I (2) unfinished" {
		t.Fatalf("%d %q", lines.Count(), lines.Line(1))
	}
}

func TestAWorkersEndRunsWhenItFails(t *testing.T) {
	a := sceneApp(t, PageEffects)
	a.client = &api.Client{}
	ran := make(chan struct{})
	a.work(func(context.Context, *api.Client) (func(), error) {
		return func() { close(ran) }, errors.New("refused")
	})
	deadline := time.Now().Add(5 * time.Second)
	for a.status == "" && time.Now().Before(deadline) {
		a.drain()
		time.Sleep(time.Millisecond)
	}
	select {
	case <-ran:
	default:
		t.Fatal("what was set going was never put back")
	}
	if a.status != "refused" || !a.failed {
		t.Fatalf("%q", a.status)
	}
}

func TestBackgroundFailuresOnlyShowOnTheRail(t *testing.T) {
	a := sceneApp(t, PageEffects)
	a.report("Saved.")
	before := len(a.journal)
	a.settle(errors.New("no route to host"), false)
	if a.status != "Saved." || len(a.journal) != before || a.online != stateOffline {
		t.Fatalf("%q, %d in the journal, online %v", a.status, len(a.journal), a.online)
	}
}

func TestAChangedRootGoesBackToComparingIt(t *testing.T) {
	a := sceneApp(t, PageEffects)
	a.settle(fmt.Errorf("reach the garland: %w", api.ErrWrongRoot), false)
	if a.connected || a.connect.stage != stageAddress || !a.failed {
		t.Fatalf("connected %v, stage %v, failed %v", a.connected, a.connect.stage, a.failed)
	}
}

func TestAnUploadWithNoAnswerIsNotCalledNotInstalledTooSoon(t *testing.T) {
	start := time.Now()
	file := api.UpdateFile{HasFirmware: true, Image: "new"}
	same := api.Firmware{Boot: "before", Image: "old", Version: "1"}
	watch := updateWatch{update: file, bootBefore: "before", started: start, unknown: true, cancelled: true}
	if said := watch.look(start.Add(3*time.Second), same, nil); said.finished {
		t.Fatal("the restart may be a moment away")
	}
	// Restarting: no answer, and then the file it took.
	if said := watch.look(start.Add(6*time.Second), api.Firmware{}, errors.New("no answer")); said.finished {
		t.Fatal("no answer decided something")
	}
	said := watch.look(start.Add(9*time.Second), api.Firmware{Boot: "after", Image: "new", OnTrial: true}, nil)
	if said.finished || said.failed || !strings.Contains(said.words, "confirms itself") {
		t.Fatalf("%+v", said)
	}
	said = watch.look(start.Add(70*time.Second), api.Firmware{Boot: "after", Image: "new"}, nil)
	if !said.finished || said.failed || !said.news || !strings.Contains(said.words, "confirmed") {
		t.Fatalf("%+v", said)
	}
}

// The footer counts down at every look; the journal hears of each stage once.
func TestAnUpdateSaysWhatItWaitsForOnceAndCountsDown(t *testing.T) {
	start := time.Now()
	watch := updateWatch{update: api.UpdateFile{HasFirmware: true, Image: "new"}, bootBefore: "before", started: start}
	away := []watchSaid{
		watch.look(start.Add(3*time.Second), api.Firmware{}, errors.New("no answer")),
		watch.look(start.Add(6*time.Second), api.Firmware{}, errors.New("no answer")),
	}
	if !away[0].news || away[1].news || !strings.Contains(away[1].words, "come back on the network, 6 s") {
		t.Fatalf("%+v", away)
	}
	// The seconds left are the garland's, which began the trial before the first look saw it.
	trial := func(left int) api.Firmware {
		return api.Firmware{Boot: "after", Image: "new", OnTrial: true, TrialLeft: left}
	}
	first := watch.look(start.Add(12*time.Second), trial(53), nil)
	later := watch.look(start.Add(42*time.Second), trial(23), nil)
	last := watch.look(start.Add(75*time.Second), trial(0), nil)
	if !first.news || later.news || last.news {
		t.Fatalf("news: %v %v %v", first.news, later.news, last.news)
	}
	if !strings.Contains(first.words, "within 53 s") || !strings.Contains(later.words, "within 23 s") || !strings.Contains(last.words, "any moment") {
		t.Fatalf("%q, %q, %q", first.words, later.words, last.words)
	}
}

func TestAnUploadNothingCameOfSaysSoInTheEnd(t *testing.T) {
	start := time.Now()
	same := api.Firmware{Boot: "before", Image: "old"}
	file := api.UpdateFile{HasFirmware: true, Image: "new"}
	watch := updateWatch{update: file, bootBefore: "before", started: start, unknown: true, cancelled: true}
	watch.look(start.Add(time.Second), same, nil)
	said := watch.look(start.Add(time.Second+updateSettle), same, nil)
	if !said.finished || said.failed || !strings.Contains(said.words, "Not installed") {
		t.Fatalf("%+v", said)
	}
	// With no answer at all, it cannot say either way.
	silent := updateWatch{update: file, bootBefore: "before", started: start, unknown: true}
	said = silent.look(start.Add(updateGiveUp+time.Second), api.Firmware{}, errors.New("no answer"))
	if !said.finished || !said.failed || !strings.Contains(said.words, "not known") {
		t.Fatalf("%+v", said)
	}
}

// With the answer lost, a restart into the very bytes of the file, with no trial seen, is no proof it took
// it: the garland may have run them already, or restarted for another reason.
func TestALostAnswerAndARestartIntoTheSameBytesIsNoConfirmation(t *testing.T) {
	start := time.Now()
	file := api.UpdateFile{HasFirmware: true, Image: "same"}
	watch := updateWatch{update: file, bootBefore: "before", started: start, unknown: true}
	said := watch.look(start.Add(9*time.Second), api.Firmware{Boot: "after", Image: "same", Version: "1.1.0.107"}, nil)
	if !said.finished || !said.failed || strings.Contains(said.words, "confirmed") || !strings.Contains(said.words, "cannot be told") {
		t.Fatalf("%+v", said)
	}
	// Effects alone, other than those before, now running: it took them.
	effects := updateWatch{update: api.UpdateFile{PackDigest: "new"}, bootBefore: "before", packBefore: "old", started: start, unknown: true}
	said = effects.look(start.Add(9*time.Second), api.Firmware{Boot: "after", PackDigest: "new"}, nil)
	if !said.finished || said.failed || !strings.Contains(said.words, "confirmed") {
		t.Fatalf("%+v", said)
	}
}

func TestAnUploadThatWasAnsweredWaitsForTheRestart(t *testing.T) {
	start := time.Now()
	watch := updateWatch{update: api.UpdateFile{HasFirmware: true, Image: "new"}, bootBefore: "before", started: start}
	if said := watch.look(start.Add(updateSettle*2), api.Firmware{Boot: "before", Image: "old"}, nil); said.finished {
		t.Fatal("it said it took the file, and that is not to be given up on so soon")
	}
}

func TestOnlyAnAnswerSaysAnUploadWasRefused(t *testing.T) {
	if !installWasRefused(fmt.Errorf("send: %w", &api.Error{Status: http.StatusBadRequest})) || !installWasRefused(api.ErrWrongRoot) {
		t.Fatal("a refusal was not taken as one")
	}
	if installWasRefused(context.Canceled) || installWasRefused(errors.New("EOF")) {
		t.Fatal("no answer was taken as a refusal")
	}
}

func TestNoAnswerToAChangeOfNetworkIsNotCalledAFailure(t *testing.T) {
	if err := mayHaveTaken(errors.New("EOF")); !strings.Contains(err.Error(), "may have taken") {
		t.Fatal(err)
	}
	refused := &api.Error{Status: http.StatusBadRequest, Message: "bad name"}
	if err := mayHaveTaken(refused); err.Error() != "bad name" {
		t.Fatal(err)
	}
}

func TestLosingSomethingTakesTwoPresses(t *testing.T) {
	var button confirmClick
	now := time.Now()
	if button.press(now) {
		t.Fatal("the first press acted")
	}
	if !button.press(now.Add(time.Second)) {
		t.Fatal("the second press did nothing")
	}
	if button.press(now.Add(2 * time.Second)) {
		t.Fatal("a third press acted without asking")
	}
	if button.press(now.Add(2*time.Second + confirmWindow + time.Second)) {
		t.Fatal("a press long after the question acted")
	}
}

func TestALogFromAnotherStartIsAskedForAgain(t *testing.T) {
	var page logPage
	page.take(api.LogPart{Text: "I (1) one\nI (2) unfin", Position: 3000, Boot: "a"})
	if page.lines.Count() != 2 {
		t.Fatalf("%q", page.lines.Lines)
	}
	page.take(api.LogPart{Text: "tail of the new log\n", Position: 3500, Boot: "b"})
	if page.position != 0 || page.boot != "b" {
		t.Fatalf("position %d, boot %s", page.position, page.boot)
	}
	want := []string{"I (1) one", "I (2) unfin", "... the garland restarted ..."}
	if strings.Join(page.lines.Lines, "|") != strings.Join(want, "|") {
		t.Fatalf("%q", page.lines.Lines)
	}
}

func TestLinesDroppedWhileSendingTheLogAreTold(t *testing.T) {
	var page logPage
	page.take(api.LogPart{Text: "I (1) one\n", Boot: "a", Missed: 3})
	if len(page.lines.Lines) != 2 || !strings.Contains(page.lines.Lines[0], "3 lines") {
		t.Fatalf("%q", page.lines.Lines)
	}
}

// firmware/app/src/wordmark.h holds the letters for the glass, and the portal's build checks its own.
func TestWordmarkIsTheFirmwares(t *testing.T) {
	header, err := os.ReadFile(filepath.Join("..", "..", "firmware", "app", "src", "wordmark.h"))
	if err != nil {
		t.Fatalf("read the header: %v", err)
	}
	rows := regexp.MustCompile(`\{(\d+), (\d+), (\d+), (\d+), (\d+)\},`).FindAllSubmatch(header, -1)
	if len(rows) != len(wordmarkGlyphs) {
		t.Fatalf("%d letters in the header, %d here", len(rows), len(wordmarkGlyphs))
	}
	for letter, row := range rows {
		for column, digits := range row[1:] {
			if string(digits) != strconv.Itoa(int(wordmarkGlyphs[letter][column])) {
				t.Fatalf("letter %d column %d: %s in the header, %d here", letter, column, digits, wordmarkGlyphs[letter][column])
			}
		}
	}
}

// The portal says the same under its wordmark.
func TestTaglineIsThePortals(t *testing.T) {
	assets, err := os.ReadFile(filepath.Join("..", "..", "firmware", "app", "src", "portal_assets.h"))
	if err != nil {
		t.Fatalf("read the portal: %v", err)
	}
	for _, words := range []string{`>` + brandTagline + `</text>`, `stop-color="#fff"`, `stop-color="#3b82f6"`} {
		if !strings.Contains(string(assets), words) {
			t.Fatalf("the portal's page does not hold %s", words)
		}
	}
}

func TestTheTaglineGoesSmoothlyFromTheWordmarksWhiteToItsBlue(t *testing.T) {
	if taglineInk(0) != colorStrong || taglineInk(len(brandTagline)-1) != colorBrand {
		t.Fatalf("%v to %v", taglineInk(0), taglineInk(len(brandTagline)-1))
	}
	for letter := 1; letter < len(brandTagline); letter++ {
		if step := int(taglineInk(letter-1).R) - int(taglineInk(letter).R); step < 0 || step > 12 {
			t.Fatalf("letter %d: a step of %d in red", letter, step)
		}
	}
}

// The connect page has three stages and no connection to take.
func TestConnectStagesLayOut(t *testing.T) {
	for _, stage := range []connectStage{stageAddress, stageRoot, stageCode} {
		a := newApp("1.0.0")
		a.connect.stage = stage
		a.connect.root.Fingerprint = sceneInfo().RootFingerprint
		a.connect.code = "4821"
		a.connect.seconds = 60
		var ops op.Ops
		var router input.Router
		if size := a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight)); size.Size.X == 0 {
			t.Fatalf("stage %d drew nothing", stage)
		}
		a.stop()
	}
}

// One real trip: a page asks over HTTPS with the root pinned, and the answer comes back to the window.
func TestAnswersReachTheWindow(t *testing.T) {
	garland := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var answer any
		switch r.URL.Path {
		case "/api/v1/about":
			answer = api.About{Product: "csiGarland", API: 1, Version: "1.0.0.90"}
		case "/api/v1/state":
			answer = sceneState()
		case "/api/v1/effects":
			answer = map[string]any{"effects": sceneEffects()}
		default:
			http.NotFound(w, r)
			return
		}
		if err := json.NewEncoder(w).Encode(answer); err != nil {
			t.Errorf("answer: %v", err)
		}
	}))
	defer garland.Close()
	root := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: garland.Certificate().Raw})
	client, err := api.New(garland.URL, root, "a-token")
	if err != nil {
		t.Fatalf("client: %v", err)
	}
	a := newApp("1.0.0")
	defer a.stop()
	a.useClient(client)
	deadline := time.Now().Add(10 * time.Second)
	for time.Now().Before(deadline) {
		a.drain()
		if len(a.effects.list) > 0 && a.state.EffectName != "" {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if a.status != "" && a.failed {
		t.Fatalf("the window reports: %s", a.status)
	}
	if a.online != stateOnline {
		t.Fatal("answers came, and the rail does not say online")
	}
	if a.about.Version != "1.0.0.90" || len(a.effects.list) != len(sceneEffects()) || a.state.EffectName != "Aurora" {
		t.Fatalf("the answers did not arrive: %+v %d %+v", a.about, len(a.effects.list), a.state)
	}
}
