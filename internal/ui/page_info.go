// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Firmware, Info and Log pages.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"image"
	"io"
	"path/filepath"
	"strings"
	"sync/atomic"
	"time"

	"gioui.org/io/clipboard"
	"gioui.org/layout"
	"gioui.org/unit"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/catalog"
	"github.com/ABespalov/csigarland/internal/secret"
)

const (
	updateLookEvery = 3 * time.Second
	updateLookLimit = 5 * time.Second
	updateGiveUp    = 150 * time.Second
	// Longer than a request ahead of it on the one connection can take.
	updateBeforeLimit = 20 * time.Second
	// Long past the 1.5 s from an update taken to the restart, so that the same boot this long after
	// says that it was not taken.
	updateSettle    = 15 * time.Second
	progressBarHigh = unit.Dp(8)
)

type firmwarePage struct {
	running    api.Firmware
	choose     widget.Clickable
	install    widget.Clickable
	path       string
	file       []byte
	update     api.UpdateFile
	contents   string
	busy       bool // From sending until it has come back. The window then only watches this.
	uploading  bool
	cancel     context.CancelFunc
	endSession context.CancelFunc // Leaving this garland ends the watch, and the upload with it.
	session    context.Context    // Which send this page is showing: answers from any other are dropped.
	percent    *atomic.Int32      // How much of the file has gone, set by the upload as it goes.
	stop       widget.Clickable
	releases   releaseCatalog
	choice     uint64 // Moves with each file chosen or download begun: only the latest is taken.
	check      widget.Clickable
	checking   bool
}

func (p *firmwarePage) init() {}

func (p *firmwarePage) load(a *App) {
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		running, err := client.Firmware(ctx)
		if err != nil {
			return nil, err
		}
		return func() { p.running = running }, nil
	})
}

func (p *firmwarePage) rows(a *App) []layout.Widget {
	return []layout.Widget{func(gtx C) D {
		if p.choose.Clicked(gtx) && !p.busy {
			p.chooseFile(a)
		}
		if p.install.Clicked(gtx) && !p.busy && p.file != nil {
			p.send(a, "")
		}
		if p.stop.Clicked(gtx) && p.uploading {
			p.cancel()
		}
		if p.releases.open.Clicked(gtx) && !p.busy {
			p.fetchCatalog(a)
		}
		if p.check.Clicked(gtx) && !p.busy {
			p.checkAPI(a)
		}
		current := fmt.Sprintf("Running version %s with %d built-in effects.", p.running.Version, p.running.Effects)
		if p.running.OnTrial {
			current += " It confirms itself after its first minute, or goes back to the previous version."
		}
		rows := []layout.Widget{
			a.body(current),
			a.hint("Choose an update file, from this computer or the catalog of releases. Only what it holds is installed. The garland checks the signature, and refuses a file not signed under its root."),
		}
		buttons := []layout.Widget{
			func(gtx C) D { return a.labelButton(gtx, &p.choose, buttonOther, "open", "Choose a file", "", !p.busy) },
			func(gtx C) D {
				return a.labelButton(gtx, &p.releases.open, buttonOther, "globe", "From the catalog", "The releases published on the project's site", !p.busy && !p.releases.fetching)
			},
			func(gtx C) D {
				return a.labelButton(gtx, &p.check, buttonOther, "check", "Check the API", "Reads every route of the public API and holds each answer to what this utility reads", !p.busy && !p.checking)
			},
		}
		if p.path != "" {
			rows = append(rows, a.hint(filepath.Base(p.path)), a.body(p.contents))
			words := "Install"
			switch {
			case p.update.HasFirmware && p.update.HasEffects:
				words = "Install firmware and effects"
			case p.update.HasFirmware:
				words = "Install firmware"
			case p.update.HasEffects:
				words = "Install effects"
			}
			buttons = append(buttons, func(gtx C) D {
				return a.labelButton(gtx, &p.install, buttonMain, "upload", words, "", !p.busy && p.file != nil)
			})
			if p.uploading {
				buttons = append(buttons, func(gtx C) D { return a.labelButton(gtx, &p.stop, buttonLosing, "close", "Cancel", "", true) })
			}
		}
		rows = append(rows, func(gtx C) D { return row(gtx, buttons...) })
		rows = append(rows, p.catalogRows(a, gtx)...)
		if p.uploading && p.percent != nil {
			rows = append(rows, p.progress(a, int(p.percent.Load())))
		}
		return a.card(gtx, "", rows...)
	}}
}

// UploadText says how far an upload has gone. All of it gone is not yet taken: that is the garland's to say.
func UploadText(percent int) string {
	if percent >= 100 {
		return "Sent. Waiting for the garland's answer..."
	}
	return fmt.Sprintf("Sending: %d%%", percent)
}

func (p *firmwarePage) progress(a *App, percent int) layout.Widget {
	return func(gtx C) D {
		bar := func(gtx C) D {
			gtx.Constraints.Min.X = gtx.Constraints.Max.X
			size := image.Pt(gtx.Constraints.Max.X, gtx.Dp(progressBarHigh))
			fill(gtx, size, size.Y/2, colorQuiet)
			fill(gtx, image.Pt(size.X*min(percent, 100)/100, size.Y), size.Y/2, colorPrimary)
			return D{Size: size}
		}
		return a.column(gtx, a.hint(UploadText(percent)), bar)
	}
}

// How an update file is picked and read: stood in for by tests, which hold the reading up.
var (
	pickUpdateFile = chooseFile
	readUpdateFile = func(path string) ([]byte, error) { return readBounded(path, catalog.MaxFileBytes) }
)

func (p *firmwarePage) chooseFile(a *App) {
	if a.choosing {
		return
	}
	a.choosing = true
	// From the moment the dialog opens: a download that ends while a file is picked, or read, is no
	// longer the owner's latest word, and goes nowhere.
	p.choice++
	owner := a.owner.Load()
	go func() {
		path, err := pickUpdateFile(a.lifetime, owner, "Update file", "*.ota", "Update files (*.ota)")
		var data []byte
		var update api.UpdateFile
		if err == nil && path != "" {
			if data, err = readUpdateFile(path); err == nil {
				update, err = api.ParseUpdate(data)
			}
		}
		a.post(func() {
			a.choosing = false
			if path == "" {
				if err != nil {
					a.reportError(err)
				}
				return
			}
			p.choice++
			p.path = path
			p.file = nil
			if err != nil {
				p.contents = err.Error()
				return
			}
			p.file = data
			p.update = update
			p.contents = UpdateText(update)
			a.report("")
		})
	}()
}

// Over only when the garland has restarted, runs what was sent and has confirmed it. onlyIfNewer, for an
// offer, is the version it offered: sent only while the garland, read right before, runs an older one, as
// another program may have updated it since. Empty for a file chosen by hand, older ones among them.
func (p *firmwarePage) send(a *App, onlyIfNewer string) {
	client := a.client
	if client == nil {
		return
	}
	file := p.file
	update := p.update
	session, endSession := context.WithCancel(a.lifetime)
	upload, cancel := context.WithCancel(session)
	percent := new(atomic.Int32)
	p.percent = percent
	p.session = session
	p.endSession = endSession
	p.cancel = cancel
	p.busy = true
	p.uploading = true
	a.report("Sending...")
	// Leaving the garland makes a new page, so what comes back after that is about one since left. The
	// session itself ends with the work below, ahead of its last words reaching the window.
	post := func(fn func()) {
		a.post(func() {
			if p.session == session {
				fn()
			}
		})
	}
	go func() {
		defer endSession()
		defer cancel()
		// Read now: the garland may have restarted since the page was opened, and an older boot id
		// would make the old image, in its last moments, look like one that went back.
		ctx, cancelLook := context.WithTimeout(upload, updateBeforeLimit)
		before, err := client.Firmware(ctx)
		cancelLook()
		if err == nil && onlyIfNewer != "" && !catalog.Newer(onlyIfNewer, before.Version) {
			post(func() {
				p.uploading = false
				p.busy = false
				a.report(fmt.Sprintf("The garland runs %s by now, so %s was not sent.", before.Version, onlyIfNewer))
			})
			return
		}
		var sent bool
		if err == nil {
			sent = true
			// Not by the window's queue, which a minimized window does not empty: that would hold the upload.
			err = client.Install(upload, file, func(sent, total int64) {
				now := int32(sent * 100 / total)
				if percent.Swap(now) != now {
					a.invalidate()
				}
			})
		}
		if settled := !sent || installWasRefused(err); err != nil && settled {
			post(func() {
				p.uploading = false
				p.busy = false
				a.settle(err, false)
				if errors.Is(err, context.Canceled) {
					a.report("Not installed: cancelled.")
				} else if a.connected {
					a.reportError(fmt.Errorf("not installed: %w", err))
				}
			})
			return
		}
		watch := updateWatch{update: update, bootBefore: before.Boot, packBefore: before.PackDigest, imageBefore: before.Image, started: time.Now(),
			unknown: err != nil, cancelled: errors.Is(err, context.Canceled)}
		post(func() {
			p.uploading = false
			a.report(watch.firstWords())
		})
		for {
			select {
			case <-session.Done():
				return
			case <-time.After(updateLookEvery):
			}
			ctx, cancel := context.WithTimeout(session, updateLookLimit)
			now, err := client.Firmware(ctx)
			cancel()
			said := watch.look(time.Now(), now, err)
			post(func() {
				a.noteAnswer(err)
				if err == nil {
					p.running = now
					a.about.Version = now.Version
					a.about.OnTrial = now.OnTrial
				}
				switch {
				case said.words == "":
				case said.failed:
					a.reportError(errors.New(said.words))
				case said.news:
					a.report(said.words)
				default:
					a.progress(said.words)
				}
				if said.finished {
					p.busy = false
					// A firmware that came up and confirmed itself is held to the API this utility reads.
					if !said.failed {
						p.checkAPI(a)
					}
				}
			})
			if said.finished {
				return
			}
		}
	}()
}

// An answer from the garland says for certain, and so does never having reached it. Anything else may
// have come after the garland took the whole file: only it can say, and watching it will tell.
func installWasRefused(err error) bool {
	var refused *api.Error
	return errors.As(err, &refused) || errors.Is(err, api.ErrWrongRoot) || errors.Is(err, api.ErrCertificateDates)
}

// updateWatch tells how an update stands from what the garland reports after it was sent.
type updateWatch struct {
	update        api.UpdateFile
	bootBefore    string
	packBefore    string // The effects running before the send.
	imageBefore   string // And the firmware.
	started       time.Time
	unknown       bool // The upload ended with no answer, so it may have been taken or not.
	sawTrial      bool // A trial of the file's image was seen, which only taking the file starts.
	cancelled     bool // And that was the owner's doing.
	sameBootSince time.Time
	stage         string // What the last words were about, for the journal to hear of each once.
}

// What a look at the garland says. The footer shows the words at every look, as they count down, and
// the journal takes them only when they begin a stage, rather than every few seconds.
type watchSaid struct {
	words    string
	news     bool
	failed   bool
	finished bool
}

func (w *updateWatch) end(words string, failed bool) watchSaid {
	return watchSaid{words: words, news: true, failed: failed, finished: true}
}

func (w *updateWatch) say(stage, words string) watchSaid {
	news := stage != w.stage
	w.stage = stage
	return watchSaid{words: words, news: news}
}

func wholeSeconds(span time.Duration) int { return int(span.Round(time.Second) / time.Second) }

func (w *updateWatch) firstWords() string {
	switch {
	case w.cancelled:
		return "Stopped sending. Looking at what the garland made of it..."
	case w.unknown:
		return "No answer to the upload, so looking at what the garland made of it..."
	default:
		return "Sent. The garland is restarting..."
	}
}

func (w *updateWatch) look(now time.Time, running api.Firmware, err error) watchSaid {
	late := now.After(w.started.Add(updateGiveUp))
	if err != nil {
		w.sameBootSince = time.Time{}
		switch {
		case late && w.unknown:
			return w.end("The garland did not answer, so it is not known whether this was installed. Check its screen, and the version here later.", true)
		case late:
			return w.end("The garland did not come back. Check its screen.", true)
		}
		return w.say("away", fmt.Sprintf("Waiting for the garland to come back on the network, %d s so far...", wholeSeconds(now.Sub(w.started))))
	}
	switch api.JudgeUpdate(w.update, w.bootBefore, running) {
	case api.UpdateWentBack:
		if w.unknown {
			return w.end(fmt.Sprintf("Not installed: running %s, not the firmware in this file.", running.Version), true)
		}
		return w.end(fmt.Sprintf("Not installed: running %s, not the firmware in this file. It went back to the previous firmware.", running.Version), true)
	case api.UpdateWrongEffects:
		return w.end("Not installed: the garland is not running the effects in this file.", true)
	case api.UpdateConfirmed:
		// With the upload's answer lost, the bytes running say nothing of this send: the garland may have
		// restarted for another reason, already running them, or another program sent the same. Only a trial
		// of the file's image seen, or effects alone that were not running before, say it took it.
		tookEffects := w.update.Image == "" && w.update.PackDigest != "" && w.packBefore != w.update.PackDigest && running.Image == w.imageBefore
		if w.unknown && !w.sawTrial && !tookEffects {
			return w.end(fmt.Sprintf("The garland restarted and runs %s. The upload got no answer, so whether it took this file cannot be told from here: send it again if need be.", running.Version), true)
		}
		return w.end("Update finished and confirmed.", false)
	case api.UpdateOnTrial:
		// Only a file with firmware starts a trial: one of effects alone did not start this one.
		if w.update.Image != "" {
			w.sawTrial = true
		}
		if late {
			return w.end("Running the new firmware, but it has not confirmed itself yet.", true)
		}
		// What is left, as the garland counts it: it began the trial, and knows its length.
		words := "Running the new firmware on trial: it confirms itself any moment now..."
		if running.TrialLeft > 0 {
			words = fmt.Sprintf("Running the new firmware on trial: it confirms itself within %d s...", running.TrialLeft)
		}
		return w.say("trial", words)
	}
	// The same start as before.
	if w.unknown {
		if w.sameBootSince.IsZero() {
			w.sameBootSince = now
		}
		if now.Sub(w.sameBootSince) >= updateSettle {
			return w.end("Not installed: the garland has gone on running what it ran.", !w.cancelled)
		}
		return watchSaid{}
	}
	if late {
		return w.end("The garland did not come back. Check its screen.", true)
	}
	return watchSaid{}
}

type clientRow struct{ forget confirmClick }

type infoPage struct {
	info     api.Info
	clients  []api.PairedClient
	rowsOf   map[int]*clientRow
	copyMAC  widget.Clickable
	copyRoot widget.Clickable
	backup   backupCard
}

func (p *infoPage) init() { p.rowsOf = map[int]*clientRow{} }

func (p *infoPage) load(a *App) {
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		info, err := client.Info(ctx)
		if err != nil {
			return nil, err
		}
		clients, err := client.Clients(ctx)
		if err != nil {
			return nil, err
		}
		return func() {
			p.info = info
			p.clients = clients
		}, nil
	})
}

func dateText(epoch int64) string {
	if epoch == 0 {
		return "when the clock is set"
	}
	return time.Unix(epoch, 0).Format("2 Jan 2006")
}

func (p *infoPage) rows(a *App) []layout.Widget {
	i := p.info
	rows := []layout.Widget{
		func(gtx C) D {
			return a.card(gtx, "Device", a.fact("Model", i.Model), a.fact("Chip", i.Chip), a.copyFact("MAC", i.MAC, &p.copyMAC))
		},
		func(gtx C) D {
			return a.card(gtx, "Memory",
				a.fact("RAM", fmt.Sprintf("%s free of %s", Kilobytes(i.RAMFree), Kilobytes(i.RAMTotal))),
				a.fact("RAM, lowest", Kilobytes(i.RAMLowest)+" free since the start"),
				a.fact("Flash", Kilobytes(i.FlashTotal)+" in all"),
				a.fact("Firmware", fmt.Sprintf("%s of its %s slot, %s free", Kilobytes(i.FirmwareUsed), Kilobytes(i.FirmwareSlot), Kilobytes(i.FirmwareSlot-i.FirmwareUsed))),
			)
		},
		func(gtx C) D {
			ip := i.IP
			if ip == "" {
				ip = "Not connected to a home network"
			}
			rows := []layout.Widget{a.fact("IP address", ip), a.fact("mDNS name", i.Hostname)}
			if !i.HTTPS {
				rows = append(rows, a.fact("HTTPS", "Not working: install a new firmware from the Firmware page."))
			}
			if !i.LedOutput {
				rows = append(rows, a.fact("LED output", "Not working: frames are not reaching all of the garland."))
			}
			rows = append(rows, a.copyFact("Root SHA-256", i.RootFingerprint, &p.copyRoot), a.fact("Certificate", "Until "+dateText(i.CertificateUntil)))
			return a.card(gtx, "Network", rows...)
		},
		func(gtx C) D {
			return a.card(gtx, "Effects and mode",
				a.fact("Effects", fmt.Sprintf("%d, %d in favourites", i.Effects, i.Favorites)),
				a.fact("Next action", NextText(i.Next)),
			)
		},
		p.backupWidget(a),
		p.clientsCard(a),
	}
	return rows
}

func (p *infoPage) clientsCard(a *App) layout.Widget {
	return func(gtx C) D {
		rows := []layout.Widget{}
		for _, paired := range p.clients {
			widgets := p.rowsOf[paired.ID]
			if widgets == nil {
				widgets = &clientRow{}
				p.rowsOf[paired.ID] = widgets
			}
			if widgets.forget.confirmed(gtx) {
				p.forget(a, paired)
			}
			name := paired.Label
			if paired.This {
				name += " (this one)"
			}
			rows = append(rows, func(gtx C) D {
				return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
					layout.Flexed(1, func(gtx C) D {
						return a.column(gtx, a.body(name), a.hint("Until "+dateText(paired.Expires)))
					}),
					layout.Rigid(func(gtx C) D { return a.confirmButton(gtx, &widgets.forget, buttonLosing, "unlink", "Forget", true) }),
				)
			})
		}
		rows = append(rows, a.hint("Holding POWER and SPEED on the garland for 10 s forgets them all."))
		return a.card(gtx, "What has access", rows...)
	}
}

func (p *infoPage) forget(a *App, paired api.PairedClient) {
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		left, err := client.ForgetClient(ctx, paired.ID)
		// Told it no longer knows this program, of this program: which is what was asked for.
		if paired.This && api.IsNotPaired(err) {
			return func() { p.unpairThis(a) }, nil
		}
		if err != nil {
			return nil, err
		}
		return func() {
			p.clients = left
			if paired.This {
				p.unpairThis(a)
			}
		}, nil
	})
}

// The garland forgot this program, so nothing of it is kept here.
func (p *infoPage) unpairThis(a *App) {
	fingerprint := a.current.Fingerprint
	a.keyring(func() func() {
		err := tokenDelete(fingerprint)
		return func() {
			if err != nil && !errors.Is(err, secret.ErrNotFound) {
				a.reportError(err)
			}
			a.connect.refreshPaired(a)
		}
	})
	a.settings.Forget(a.current.Fingerprint)
	if err := a.saveSettings(); err != nil {
		a.reportError(err)
	}
	a.disconnect()
	a.connect.start(a)
}

type logPage struct {
	lines    LogLines
	position uint32
	boot     string
	busy     bool
	follow   widget.Bool
	clear    widget.Clickable
	copyAll  widget.Clickable
	list     layout.List
}

const logViewHeight = unit.Dp(620)

func (p *logPage) init() {
	p.follow.Value = true
	p.list.Axis = layout.Vertical
	p.list.ScrollToEnd = true
}

func (p *logPage) load(a *App) { p.poll(a) }

func (p *logPage) poll(a *App) {
	if p.busy {
		return
	}
	p.busy = true
	after := p.position
	a.background(func(ctx context.Context, client *api.Client) (func(), error) {
		part, err := client.Log(ctx, after)
		return func() {
			p.busy = false
			if err == nil {
				p.take(part)
			}
		}, err
	})
}

// A position means nothing in another start of the garland, so what came with one is dropped and
// the next look asks for the new log from its beginning.
func (p *logPage) take(part api.LogPart) {
	if p.boot != "" && part.Boot != p.boot {
		p.boot = part.Boot
		p.position = 0
		p.lines.Restarted()
		return
	}
	p.boot = part.Boot
	p.position = part.Position
	p.lines.Missed(part.Missed)
	p.lines.Add(part.Text, part.Lost)
}

func (p *logPage) rows(a *App) []layout.Widget {
	return []layout.Widget{func(gtx C) D {
		if p.clear.Clicked(gtx) {
			p.lines.Clear()
		}
		if p.copyAll.Clicked(gtx) {
			gtx.Execute(clipboard.WriteCmd{Type: "application/text", Data: io.NopCloser(strings.NewReader(p.lines.Text()))})
			a.report("The log is on the clipboard.")
		}
		p.follow.Update(gtx)
		// Only once the lines fill the view: until then they start at its top.
		p.list.ScrollToEnd = p.follow.Value && p.lines.Count() > p.list.Position.Count
		return a.card(gtx, "",
			a.hint("The garland starts keeping its log at the first look here, in a small buffer, so what came before then is not there. It holds no password, token or key."),
			func(gtx C) D {
				return row(gtx,
					func(gtx C) D { return a.checkbox(gtx, &p.follow, "Follow the end") },
					func(gtx C) D {
						return a.labelButton(gtx, &p.copyAll, buttonOther, "copy", "Copy all", "", p.lines.Count() > 0)
					},
					func(gtx C) D {
						return a.labelButton(gtx, &p.clear, buttonOther, "eraser", "Clear", "", p.lines.Count() > 0)
					},
				)
			},
			func(gtx C) D {
				gtx.Constraints.Max.Y = gtx.Dp(logViewHeight)
				gtx.Constraints.Min.Y = gtx.Constraints.Max.Y
				gtx.Constraints.Min.X = gtx.Constraints.Max.X
				return p.list.Layout(gtx, p.lines.Count(), func(gtx C, i int) D {
					style := a.label(sizeSmall, colorText, p.lines.Line(i))
					style.Font.Typeface = "Go Mono"
					return style.Layout(gtx)
				})
			},
		)
	}}
}
