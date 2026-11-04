// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The window: its loop, its pages, and how work reaches the garland and comes back.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"sync/atomic"
	"time"

	"gioui.org/app"
	"gioui.org/f32"
	"gioui.org/font"
	"gioui.org/io/input"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"
	"github.com/rs/zerolog/log"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/secret"
	"github.com/ABespalov/csigarland/internal/settings"
)

// Page is what the window shows.
type Page int

// The pages, in the order of the tabs. The portal's six, then what only the utility has.
const (
	PageEffects Page = iota
	PageSchedule
	PageDevice
	PageNetwork
	PageFirmware
	PageInfo
	PageLog
	pageCount
)

var pageNames = [pageCount]string{"Effects", "Schedule", "Device", "Network", "Firmware", "Info", "Log"}

const (
	windowTitle  = "csiGarland"
	windowClass  = "csiGarlandUtility"
	queueLength  = 64
	requestLimit = 30 * time.Second
	pollEvery    = 3 * time.Second
	aboutEvery   = time.Minute // With no stream: see refreshState.
	logPollEvery = 2 * time.Second
	windowWidth  = unit.Dp(1040)
	windowHeight = unit.Dp(860)
	// As csiPass has them, in its desktop.MinWidth and MinHeight.
	windowMinWidth  = unit.Dp(1024)
	windowMinHeight = unit.Dp(768)
)

// App is the window's whole state. Only the window's goroutine touches it: see post.
type App struct {
	theme      *material.Theme
	invalidate func()
	queue      chan func()
	lifetime   context.Context
	stop       context.CancelFunc
	// What waits or runs for the garland connected to ends when that is left.
	connection    context.Context
	endConnection context.CancelFunc

	settingsPath string
	settings     settings.Settings
	current      settings.Garland // The one connected to.
	// Its address came from a search of the network and differs from the one kept: anyone can answer a
	// search, so it is kept only once the garland has answered there under its own root.
	unproven bool
	// The window's own handle, for a system dialog to belong to. Read away from the window.
	owner     atomic.Uintptr
	version   string
	client    *api.Client
	connected bool
	page      Page
	tabs      [pageCount]widget.Clickable
	list      widget.List
	status    string
	failed    bool
	online    onlineState
	rescan    widget.Clickable
	leave     widget.Clickable

	// One look at a time, and at most one waiting for a frame, which a minimized window may not draw.
	stateAsked bool
	stateTick  atomic.Bool // These two are set away from the window, in poll.
	logTick    atomic.Bool
	choosing   bool // The system's file dialog is open.
	lastWork   chan struct{}

	local      bool // Working on effects with no garland.
	toConnect  widget.Clickable
	modal      modalKind
	modalAsked uint64 // Which asking for a window is the latest: only that one's answer opens it.
	preview    previewWindow
	offer      updateOffer

	// What the pointer is over, for the tooltip.
	frames     uint64       // Laid out so far.
	source     input.Source // The window's events, live under a modal window too: see hovered.
	hovers     map[*widget.Clickable]*buttonHover
	pointerAt  f32.Point
	tipOffered string
	tipWords   string
	tipSince   time.Time

	journal       []report
	journalOpen   bool
	journalUnseen int
	journalButton widget.Clickable

	about   api.About
	state   api.State
	pushing bool // A stream of the garland's state is open: see listen.
	// A state is to be asked for though a stream is open, as one pushed was passed over: see rereadState.
	// Kept until a state is taken, through a request that failed.
	mustRead bool
	// Counts the states taken from the stream: one asked for before the count moved is older news.
	pushes int
	// What the garland says of itself is to be asked for again, as after a stream opened again, until it is.
	aboutStale bool
	aboutAt    time.Time // When About was last asked for by refreshState.
	stateAt    time.Time // When the garland's state last came, pushed or asked for.
	trialImage string    // The image last seen on trial, see takeAbout.

	connect  connectPage
	effects  effectsPage
	tune     tunePage
	schedule schedulePage
	device   devicePage
	network  networkPage
	firmware firmwarePage
	info     infoPage
	logPage  logPage
}

func newApp(version string) *App {
	lifetime, stop := context.WithCancel(context.Background())
	a := &App{
		theme:      newTheme(),
		invalidate: func() {},
		queue:      make(chan func(), queueLength),
		hovers:     map[*widget.Clickable]*buttonHover{},
		lifetime:   lifetime,
		stop:       stop,
		version:    version,
	}
	a.connection, a.endConnection = context.WithCancel(lifetime)
	a.list.Axis = layout.Vertical
	a.connect.init()
	a.tune.init()
	a.preview.init()
	a.newPages()
	return a
}

// Every page that shows or would send what one garland has. Tune holds the owner's own file.
func (a *App) newPages() {
	a.about = api.About{}
	a.state = api.State{}
	a.stateAsked = false
	a.offer = updateOffer{}
	if a.modal == modalOffer {
		a.modal = modalNone
	}
	a.effects = effectsPage{}
	a.effects.init()
	a.schedule = schedulePage{}
	a.schedule.init()
	a.device = devicePage{}
	a.device.init()
	a.network = networkPage{}
	a.network.init()
	a.firmware = firmwarePage{}
	a.firmware.init()
	a.info = infoPage{}
	a.info.init()
	a.logPage = logPage{}
	a.logPage.init()
}

// post runs fn on the window's goroutine at its next frame.
func (a *App) post(fn func()) {
	select {
	case a.queue <- fn:
		a.invalidate()
	case <-a.lifetime.Done():
	}
}

func (a *App) drain() {
	for {
		select {
		case fn := <-a.queue:
			fn()
		default:
			return
		}
	}
}

func (a *App) report(words string) {
	a.status = words
	a.failed = false
	if words != "" {
		a.addReport(words, false)
	}
}

// progress says how something long goes on, in the footer alone: the journal heard when it began.
func (a *App) progress(words string) {
	a.status = words
	a.failed = false
}

func (a *App) reportError(err error) {
	a.status = err.Error()
	a.failed = true
	a.addReport(a.status, true)
}

// work runs fn away from the window, and then what it returns on the window's goroutine, whether it
// failed or not, so that what fn set going can always be put back. A failure is then reported. Works
// go one after another in the order asked for, or the owner's Off could overtake the On before it.
func (a *App) work(fn func(ctx context.Context, client *api.Client) (func(), error)) {
	a.run(fn, true, true)
}

// aside is work the owner asked for that takes its time, and which others need not wait for.
func (a *App) aside(fn func(ctx context.Context, client *api.Client) (func(), error)) {
	a.run(fn, true, false)
}

// background is work that nobody asked for just now: the rail shows if it fails, and no more.
func (a *App) background(fn func(ctx context.Context, client *api.Client) (func(), error)) {
	a.run(fn, false, false)
}

func (a *App) run(fn func(ctx context.Context, client *api.Client) (func(), error), reporting, ordered bool) {
	client := a.client
	if client == nil {
		return
	}
	var before chan struct{}
	done := make(chan struct{})
	if ordered {
		before = a.lastWork
		a.lastWork = done
	}
	connection := a.connection
	go func() {
		defer close(done)
		if before != nil {
			select {
			case <-before:
			case <-connection.Done():
			}
		}
		// Sent to a garland since left, a command would do what nobody now wants of it.
		if connection.Err() != nil {
			return
		}
		ctx, cancel := context.WithTimeout(connection, requestLimit)
		defer cancel()
		done, err := fn(ctx, client)
		a.post(func() {
			// From a garland since left, whose pages are gone along with what this set going.
			if client != a.client {
				return
			}
			if done != nil {
				done()
			}
			a.settle(err, reporting)
		})
	}()
}

// settle is what any failure means for the window as a whole.
func (a *App) settle(err error, reporting bool) {
	a.noteAnswer(err)
	switch {
	case err == nil:
		a.proveAddress()
	case a.connected && a.unproven && errors.Is(err, api.ErrWrongRoot):
		a.strangerAnswered()
	case a.connected && api.IsNotPaired(err):
		a.lostPairing()
	case a.connected && errors.Is(err, api.ErrWrongRoot):
		a.lostRoot()
	// Nothing works until a clock is put right, so it is said even of what nobody asked for just now.
	case reporting || errors.Is(err, api.ErrCertificateDates):
		if !errors.Is(err, context.Canceled) {
			a.reportError(err)
		}
	}
}

// The garland has answered under its own root at the address the search found, so that is where it is.
func (a *App) proveAddress() {
	if !a.unproven {
		return
	}
	a.unproven = false
	if err := a.noteUsed(); err != nil {
		a.reportError(fmt.Errorf("the garland's new address could not be remembered: %w", err))
	}
}

// Something at an address the search found answered as a garland met before, but without its root. It
// may be nothing of the owner's, so trusting it is not offered, and its address is not kept.
func (a *App) strangerAnswered() {
	address := a.current.Address
	a.disconnect()
	a.connect.start(a)
	a.reportError(fmt.Errorf("%s answered the search as this garland, but without its root, so it was left: connect by the address you know", address))
}

// The garland no longer knows this program: its 100 days ran out, or it was forgotten.
func (a *App) lostPairing() {
	if err := secret.Delete(a.current.Fingerprint); err != nil {
		log.Warn().Err(err).Msg("a token that no longer works stayed")
	}
	a.disconnect()
	a.connect.start(a)
	a.connect.refreshPaired(a)
	a.reportError(errors.New("the garland no longer has this program paired: pair it again"))
}

// As after the owner set a domain, when the garland makes a new root. Only the owner can say whether
// to trust that, so this goes back to comparing it, and pairing again.
func (a *App) lostRoot() {
	a.disconnect()
	a.connect.start(a)
	a.reportError(errors.New("the garland's certificate no longer comes from the root kept for it: connect, and compare its SHA-256 again"))
}

func (a *App) disconnect() {
	a.endConnection()
	a.connection, a.endConnection = context.WithCancel(a.lifetime)
	a.connected = false
	a.pushing = false
	if a.client != nil {
		a.client.Close()
		a.client = nil
	}
	a.current = settings.Garland{}
	a.unproven = false
	a.lastWork = nil
	if a.firmware.endSession != nil {
		a.firmware.endSession()
	}
	a.newPages()
}

func (a *App) saveSettings() error {
	if err := settings.Save(a.settingsPath, a.settings); err != nil {
		return err
	}
	return nil
}

// noteUsed makes the current garland the last one used, which is the first in the list.
func (a *App) noteUsed() error {
	a.current.LastUsed = time.Now().Unix()
	a.settings.Remember(a.current)
	return a.saveSettings()
}

// connectKnown goes to a garland paired before, under the root kept for it, with no questions. An
// address other than the one kept is kept only once the garland answers there: see proveAddress.
func (a *App) connectKnown(garland settings.Garland, address, token string) error {
	client, err := api.New(address, []byte(garland.RootPEM), token)
	if err != nil {
		return err
	}
	a.current = garland
	saved := a.noteUsed()
	a.current.Address = address
	a.unproven = address != garland.Address
	a.useClient(client)
	return saved
}

// Back to a garland paired before, if there is one.
func (a *App) resume() {
	path, err := settings.DefaultPath()
	if err != nil {
		a.reportError(err)
		return
	}
	a.settingsPath = path
	loaded, err := settings.Load(path)
	if err != nil {
		a.reportError(err)
		return
	}
	a.settings = loaded
	a.connect.refreshPaired(a)
	last, known := loaded.Last()
	if !known {
		return
	}
	a.connect.address.SetText(last.Address)
	token, err := secret.Get(last.Fingerprint)
	if err != nil {
		return
	}
	if err := a.connectKnown(last, last.Address, token); err != nil {
		a.reportError(err)
	}
}

func (a *App) useClient(client *api.Client) {
	a.local = false
	a.client = client
	a.connected = true
	a.online = stateChecking
	a.open(a.page)
	a.lookForUpdate()
	a.listen()
}

func (a *App) refreshAbout() {
	a.background(func(ctx context.Context, client *api.Client) (func(), error) {
		about, err := client.About(ctx)
		return func() {
			if err == nil {
				a.takeAbout(about)
			}
		}, err
	})
}

func (a *App) open(page Page) {
	a.page = page
	a.schedule.popup = popupNone
	a.journalOpen = false
	a.status = ""
	a.failed = false
	a.refreshAbout()
	switch page {
	case PageEffects:
		a.effects.load(a)
	case PageSchedule:
		a.schedule.load(a)
	case PageDevice:
		a.device.load(a)
	case PageNetwork:
		a.network.load(a)
	case PageFirmware:
		a.firmware.load(a)
	case PageInfo:
		a.info.load(a)
	case PageLog:
		a.logPage.load(a)
	}
}

// What changes by itself, or by the garland's own buttons, is looked at now and then.
func (a *App) poll() {
	state := time.NewTicker(pollEvery)
	logs := time.NewTicker(logPollEvery)
	defer state.Stop()
	defer logs.Stop()
	for {
		select {
		case <-a.lifetime.Done():
			return
		case <-state.C:
			a.tick(&a.stateTick, a.refreshState)
		case <-logs.C:
			a.tick(&a.logTick, func() {
				if a.page == PageLog {
					a.logPage.poll(a)
				}
			})
		}
	}
}

// While an update goes up the garland can answer nothing else, and the page is watching it anyway.
func (a *App) tick(queued *atomic.Bool, look func()) {
	if !queued.CompareAndSwap(false, true) {
		return
	}
	a.post(func() {
		queued.Store(false)
		if a.connected && !a.firmware.busy {
			look()
		}
	})
}

func (a *App) refreshState() {
	if a.effects.sending || a.client == nil || a.stateAsked {
		return
	}
	// What a stream brings need not be asked for, but what is said of a firmware on trial, or what a
	// change of the window's own may have missed while it was on its way: see rereadState.
	if a.pushing && !a.mustRead && !a.aboutStale && a.about.Version != "" && !a.about.OnTrial {
		return
	}
	a.stateAsked = true
	changes, pushes := a.effects.changes, a.pushes
	// What comes with a restart or a confirmation, and nothing else tells of them. With no stream, as
	// with both of the garland's taken by others, now and then too: an update sent from elsewhere
	// restarts it with nothing else to say so.
	wantAbout := a.about.Version == "" || a.about.OnTrial || a.aboutStale || (!a.pushing && time.Since(a.aboutAt) >= aboutEvery)
	if wantAbout {
		a.aboutAt = time.Now()
	}
	a.background(func(ctx context.Context, client *api.Client) (func(), error) {
		state, err := client.State(ctx)
		var about api.About
		aboutErr := errors.New("not asked")
		if err == nil && wantAbout {
			about, aboutErr = client.About(ctx)
		}
		return func() {
			a.stateAsked = false
			// Asked before a change made since, or with one on its way, so it could only take that back:
			// the next look asks again, as a state pushed meanwhile may have been passed over too. A state
			// pushed and taken since is newer, and says all there is.
			switch {
			case err != nil:
			case pushes != a.pushes:
			case changes == a.effects.changes && a.effects.commands == 0:
				a.mustRead = false
				a.takeState(state)
			default:
				a.mustRead = true
			}
			if aboutErr == nil {
				a.takeAbout(about)
			}
		}, err
	})
}

// takeAbout keeps what the garland says of itself, and while its firmware is on trial says in the footer
// when it confirms itself: after a restart the update's own watch is gone, or never ran here.
func (a *App) takeAbout(about api.About) {
	was := a.about.OnTrial
	// Another firmware may have routes the one before had not, as 1.1.0's day of 1.0.0: asked again.
	if about.Image != a.about.Image {
		a.schedule.timelineNone = false
	}
	if about.OnTrial {
		// The image on trial, so that the end of the trial can be told apart: the same image confirmed,
		// or another, the one before, gone back to.
		a.trialImage = about.Image
	}
	a.about = about
	a.aboutStale = false
	if a.firmware.busy {
		return
	}
	switch {
	case about.OnTrial:
		a.progress(TrialText(about.TrialLeft))
	case was && a.trialImage != "" && about.Image != a.trialImage:
		a.reportError(fmt.Errorf("the new firmware did not confirm itself: the garland went back to %s", about.Version))
		a.trialImage = ""
	case was:
		a.report("The new firmware confirmed itself.")
		a.trialImage = ""
	}
}

// TrialText says how long a firmware on trial has before it confirms itself.
func TrialText(left int) string {
	if left <= 0 {
		return "The firmware is on trial: it confirms itself any moment now..."
	}
	return fmt.Sprintf("The firmware is on trial: it confirms itself within %d s...", left)
}

func (a *App) takeState(state api.State) {
	// A change of the power since the day was read is a step done, or a choice: the day is read again.
	if state.Power != a.state.Power {
		a.schedule.timelineTried = time.Time{}
	}
	a.state = state
	a.stateAt = time.Now()
	a.effects.markActive(state.Effect)
}

// Frame lays out the whole window.
func (a *App) Frame(gtx C) D {
	a.frames++
	a.source = gtx.Source
	defer a.forgetHovers()
	a.drain()
	fill(gtx, gtx.Constraints.Max, 0, colorBackground)
	for i := range a.tabs {
		if a.tabs[i].Clicked(gtx) && !a.firmware.busy {
			a.choosePage(Page(i))
		}
	}
	if a.leave.Clicked(gtx) && a.connected && !a.firmware.busy {
		a.leaveGarland()
	}
	// Whatever is under a modal window, or a page's own, stays where it is and takes no presses or
	// keys: a field focused before the window opened would otherwise go on taking what is typed.
	under := gtx
	if a.modal != modalNone || a.schedulePopupShown() {
		under = gtx.Disabled()
	}
	title, rows := a.pageNow()
	dims := layout.Flex{Axis: layout.Vertical}.Layout(under,
		layout.Rigid(a.topBar),
		layout.Rigid(a.rail),
		layout.Flexed(1, func(gtx C) D { return a.layoutPage(gtx, title, rows) }),
		layout.Rigid(a.footer),
	)
	a.layoutModal(gtx)
	a.layoutTip(gtx)
	a.trackPointer(gtx)
	return dims
}

// With no garland, only the built-in effects are there to go to.
func (a *App) pageAvailable(page Page) bool {
	return a.connected || page == PageEffects
}

// Whether a page of the menu is what the window shows, rather than the connect page or the journal.
func (a *App) pageShown() bool {
	return (a.connected || a.local) && !a.journalOpen
}

// Whether a window of the Schedule page is over it, which is only while that page is shown.
func (a *App) schedulePopupShown() bool {
	return a.pageShown() && a.page == PageSchedule && a.schedule.popup != popupNone
}

func (a *App) choosePage(page Page) {
	switch {
	case !a.pageAvailable(page):
		return
	case !a.connected:
		a.journalOpen = false
		if !a.local {
			a.enterLocal()
		}
		return
	}
	if page != a.page {
		a.list.Position = layout.Position{}
	}
	a.open(page)
}

// To the list of garlands, this one still paired and first in it.
func (a *App) leaveGarland() {
	a.disconnect()
	a.journalOpen = false
	a.report("")
	a.connect.start(a)
}

func (a *App) enterLocal() {
	a.connect.pairing.Add(1)
	a.connect.busy = false
	a.newPages()
	a.local = true
	a.journalOpen = false
	a.page = PageEffects
	a.report("")
}

func (a *App) leaveLocal() {
	a.local = false
	a.journalOpen = false
	a.newPages()
	a.connect.start(a)
}

func (a *App) pageNow() (string, []layout.Widget) {
	switch {
	case a.journalOpen:
		return "Journal", a.journalRows()
	case a.local:
		return "Effects", a.effects.rows(a)
	case !a.connected:
		return "", a.connect.rows(a)
	}
	var rows []layout.Widget
	switch a.page {
	case PageEffects:
		rows = a.effects.rows(a)
	case PageSchedule:
		rows = a.schedule.rows(a)
	case PageDevice:
		rows = a.device.rows(a)
	case PageNetwork:
		rows = a.network.rows(a)
	case PageFirmware:
		rows = a.firmware.rows(a)
	case PageInfo:
		rows = a.info.rows(a)
	case PageLog:
		rows = a.logPage.rows(a)
	}
	return pageNames[a.page], rows
}

func (a *App) layoutPage(gtx C, title string, rows []layout.Widget) D {
	return layout.Inset{Top: gapLarge, Left: gapPage, Right: gapSmall}.Layout(gtx, func(gtx C) D {
		gtx.Constraints.Min.X = gtx.Constraints.Max.X
		return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
			layout.Rigid(func(gtx C) D {
				if title == "" {
					return D{}
				}
				style := a.label(sizeTitle, colorStrong, title)
				style.Font.Weight = font.Bold
				return layout.Inset{Bottom: gap}.Layout(gtx, style.Layout)
			}),
			layout.Flexed(1, func(gtx C) D {
				return material.List(a.theme, &a.list).Layout(gtx, len(rows), func(gtx C, i int) D {
					return layout.Inset{Bottom: gap, Right: gap}.Layout(gtx, rows[i])
				})
			}),
		)
	})
}

// Run shows the window, and does not come back: app.Main never does. exit ends the process.
func Run(version string, exit func(code int)) {
	if !firstInstance() {
		focusFirstInstance()
		exit(0)
		return
	}
	a := newApp(version)
	go func() {
		app.ID = windowClass
		window := new(app.Window)
		window.Option(app.Title(windowTitle), app.Size(windowWidth, windowHeight), app.MinSize(windowMinWidth, windowMinHeight))
		// Ahead of anything that could post, as that reads it from another goroutine.
		a.invalidate = window.Invalidate
		a.resume()
		if !a.connected {
			a.connect.discover(a)
		}
		go a.poll()
		var ops op.Ops
		for {
			switch event := window.Event().(type) {
			case app.DestroyEvent:
				a.stop()
				code := 0
				if event.Err != nil {
					log.Error().Err(event.Err).Msg("the window failed")
					code = 1
				}
				exit(code)
				return
			case app.FrameEvent:
				gtx := app.NewContext(&ops, event)
				a.Frame(gtx)
				event.Frame(gtx.Ops)
			case app.ViewEvent:
				a.owner.Store(viewHandle(event))
			}
		}
	}()
	app.Main()
}
