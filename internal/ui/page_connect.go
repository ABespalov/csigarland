// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Reaching a garland for the first time: its address, its root, and pairing.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"image"
	"os"
	"slices"
	"sync/atomic"
	"time"

	"gioui.org/layout"
	"gioui.org/unit"
	"gioui.org/widget"
	"github.com/rs/zerolog/log"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/settings"
)

const (
	pairPollEvery  = 1500 * time.Millisecond
	foundShown     = 12
	pairedMarkSize = unit.Dp(18)
)

type connectStage int

const (
	stageAddress connectStage = iota // The owner types where it is.
	stageRoot                        // The owner compares its root's fingerprint.
	stageCode                        // The owner compares the code and presses the garland's buttons.
)

type connectPage struct {
	stage    connectStage
	address  widget.Editor
	next     widget.Clickable
	trust    widget.Clickable
	back     widget.Clickable
	busy     bool
	root     api.Root
	changed  bool // A different root from the one kept for this address.
	code     string
	seconds  int
	looking  bool // A search of the network is under way.
	found    []api.Found
	looked   bool
	picks    [foundShown]widget.Clickable
	forgets  [foundShown]confirmClick
	paired   map[string]bool
	name     string // Of the garland picked from the list, to remember it by.
	again    widget.Clickable
	local    widget.Clickable
	copyRoot widget.Clickable
	pairing  atomic.Uint64 // Which attempt is current: one left behind ends by itself.
	asked    atomic.Uint64 // Which look at the tokens is current: one left behind is dropped.
	// Which choice of the owner's is current: each garland picked, forgotten, typed or left for the effects
	// moves it, and a token read for one before is dropped when it comes.
	choice atomic.Uint64
}

func (p *connectPage) init() {
	p.address = newField(true)
}

func (p *connectPage) start(a *App) {
	p.choice.Add(1)
	p.stage = stageAddress
	p.busy = false
	p.name = ""
	p.pairing.Add(1)
	p.refreshPaired(a)
	p.discover(a)
}

// discover looks for garlands, for the owner to pick from rather than type.
func (p *connectPage) discover(a *App) {
	if p.looking {
		return
	}
	p.looking = true
	go func() {
		found, err := api.Discover(a.lifetime, api.DiscoveryWait)
		a.post(func() {
			p.looking = false
			p.looked = true
			p.found = found
			if err != nil {
				log.Warn().Err(err).Msg("no search of the network")
			}
		})
	}()
}

// Which of those met before have a token, asked once rather than at every frame, and away from the window,
// as a locked store may ask for its password first.
func (p *connectPage) refreshPaired(a *App) {
	asked := p.asked.Add(1)
	garlands := slices.Clone(a.settings.Garlands)
	a.keyring(func() func() {
		paired := map[string]bool{}
		for _, garland := range garlands {
			if _, err := tokenGet(garland.Fingerprint); err == nil {
				paired[garland.Fingerprint] = true
			}
		}
		return func() {
			if p.asked.Load() == asked {
				p.paired = paired
			}
		}
	})
}

func (p *connectPage) pick(a *App, row GarlandRow) {
	garland, known := a.settings.Find(row.Fingerprint)
	if !known || !row.Paired {
		p.byAddress(a, row)
		return
	}
	// The token away from the window; a choice made meanwhile, another garland, forgetting this one, an
	// address typed or the effects alone, stands, and this one is dropped.
	choice := p.choice.Add(1)
	a.keyring(func() func() {
		token, err := tokenGet(row.Fingerprint)
		return func() {
			if p.choice.Load() != choice || a.connected {
				return
			}
			if _, still := a.settings.Find(row.Fingerprint); !still {
				return
			}
			if err != nil {
				p.byAddress(a, row)
				return
			}
			if err := a.connectKnown(garland, row.Address, token); err != nil {
				a.reportError(err)
			}
		}
	})
}

// byAddress goes to a garland by its address, comparing its root before pairing, as for one not paired.
func (p *connectPage) byAddress(a *App, row GarlandRow) {
	p.address.SetText(row.Address)
	p.name = row.Name
	p.fetchRoot(a)
}

func (p *connectPage) forgetRow(a *App, row GarlandRow) {
	p.choice.Add(1)
	a.keyring(func() func() {
		err := tokenDelete(row.Fingerprint)
		return func() {
			if err != nil {
				a.reportError(err)
			}
			p.refreshPaired(a)
		}
	})
	a.settings.Forget(row.Fingerprint)
	if err := a.saveSettings(); err != nil {
		a.reportError(err)
	}
}

func (p *connectPage) foundCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.again.Clicked(gtx) {
			p.discover(a)
		}
		list := GarlandRows(a.settings.Garlands, func(fingerprint string) bool { return p.paired[fingerprint] }, p.found)
		words := "Choose one. One paired before connects at once, and another after comparing its certificate."
		switch {
		case p.looking:
			words = "Looking on this network..."
		case len(list) == 0:
			words = "None answered. A garland needs a firmware that says what it is, and to be on this network. Its address can be typed below."
		}
		rows := []layout.Widget{a.hint(words)}
		for i := range min(len(list), foundShown) {
			row := list[i]
			if p.picks[i].Clicked(gtx) && !p.busy {
				p.pick(a, row)
			}
			if p.forgets[i].confirmed(gtx) {
				p.forgetRow(a, row)
			}
			rows = append(rows, func(gtx C) D { return p.layoutRow(gtx, a, i, row) })
		}
		rows = append(rows, func(gtx C) D {
			return a.labelButton(gtx, &p.again, buttonOther, "search", "Look again", "", !p.looking)
		})
		return a.card(gtx, "Garlands", rows...)
	}
}

func (p *connectPage) layoutRow(gtx C, a *App, i int, row GarlandRow) D {
	children := []layout.FlexChild{
		layout.Rigid(func(gtx C) D {
			size := gtx.Dp(pairedMarkSize)
			if row.Paired {
				drawGlyph(gtx, "check", size, colorGood)
			}
			return D{Size: image.Pt(size, size)}
		}),
		layout.Rigid(layout.Spacer{Width: gap}.Layout),
		layout.Flexed(1, func(gtx C) D { return a.column(gtx, a.body(row.Name), a.hint(row.Text())) }),
	}
	if row.Known {
		children = append(children,
			layout.Rigid(func(gtx C) D {
				return a.confirmButton(gtx, &p.forgets[i], buttonLosing, "trash", "Forget this garland here", true)
			}),
			layout.Rigid(layout.Spacer{Width: gapSmall}.Layout))
	}
	children = append(children, layout.Rigid(func(gtx C) D {
		return a.iconButton(gtx, &p.picks[i], buttonMain, "link", "Connect to this garland", !p.busy)
	}))
	return layout.Flex{Alignment: layout.Middle}.Layout(gtx, children...)
}

func (p *connectPage) localCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.local.Clicked(gtx) {
			a.enterLocal()
		}
		return a.card(gtx, "No garland at hand",
			a.hint("Effects can be tuned and previewed here without one, and saved to a file."),
			func(gtx C) D { return a.labelButton(gtx, &p.local, buttonOther, "tune", "Work on effects", "", true) },
		)
	}
}

func (p *connectPage) rows(a *App) []layout.Widget {
	switch p.stage {
	case stageRoot:
		return []layout.Widget{p.rootCard(a)}
	case stageCode:
		return []layout.Widget{p.codeCard(a)}
	default:
		return []layout.Widget{p.foundCard(a), p.addressCard(a), p.localCard(a)}
	}
}

func (p *connectPage) addressCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.next.Clicked(gtx) && !p.busy {
			p.name = ""
			p.fetchRoot(a)
		}
		return a.card(gtx, "Or by its address",
			a.hint("Its address, as in a browser: an IP address, a domain or a .local name, with a port if it has another. The screen on the garland shows the address."),
			func(gtx C) D { return a.field(gtx, &p.address, "192.168.1.50 or csigarland.local", 0) },
			func(gtx C) D { return a.labelButton(gtx, &p.next, buttonMain, "link", "Connect", "", !p.busy) },
		)
	}
}

func (p *connectPage) fetchRoot(a *App) {
	address, err := api.CleanAddress(p.address.Text())
	if err != nil {
		a.reportError(err)
		return
	}
	p.choice.Add(1)
	p.busy = true
	a.report("Reaching the garland...")
	generation := p.pairing.Load()
	go func() {
		ctx, cancel := context.WithTimeout(a.lifetime, requestLimit)
		defer cancel()
		root, err := api.FetchRoot(ctx, address)
		a.post(func() {
			p.busy = false
			if generation != p.pairing.Load() {
				return
			}
			if err != nil {
				a.reportError(err)
				return
			}
			a.report("")
			p.root = root
			p.changed = false
			for _, garland := range a.settings.Garlands {
				kept, err := api.CleanAddress(garland.Address)
				p.changed = p.changed || (err == nil && kept == address && garland.Fingerprint != root.Fingerprint)
			}
			p.stage = stageRoot
		})
	}()
}

func (p *connectPage) rootCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.back.Clicked(gtx) {
			p.start(a)
		}
		if p.trust.Clicked(gtx) && !p.busy {
			p.startPairing(a)
		}
		rows := []layout.Widget{
			a.hint("The garland made this root certificate for itself, so nothing else can vouch for it. Compare its SHA-256 with the one on the garland's Info page in the portal, or in the log of its first start."),
			func(gtx C) D {
				return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
					layout.Rigid(a.body(p.root.Fingerprint)),
					layout.Rigid(layout.Spacer{Width: gap}.Layout),
					layout.Rigid(func(gtx C) D { return a.copyButton(gtx, &p.copyRoot, p.root.Fingerprint) }),
				)
			},
		}
		if p.changed {
			rows = append(rows, a.statusLine("This is NOT the root kept for this address. A garland makes a new one only rarely. Trust it only if you know why it changed.", true))
		}
		rows = append(rows, func(gtx C) D {
			return row(gtx,
				func(gtx C) D {
					return a.labelButton(gtx, &p.trust, buttonMain, "key", "It matches: ask for access", "", !p.busy)
				},
				func(gtx C) D { return a.labelButton(gtx, &p.back, buttonOther, "back", "Back", "", true) },
			)
		})
		return a.card(gtx, "Is this your garland?", rows...)
	}
}

func (p *connectPage) startPairing(a *App) {
	address, err := api.CleanAddress(p.address.Text())
	if err != nil {
		a.reportError(err)
		return
	}
	client, err := api.New(address, p.root.PEM, "")
	if err != nil {
		a.reportError(err)
		return
	}
	p.busy = true
	generation := p.pairing.Add(1)
	host, _ := os.Hostname()
	go func() {
		ctx, cancel := context.WithTimeout(a.lifetime, requestLimit)
		defer cancel()
		var asked api.PairRequest
		const older = "this garland does not answer as the public API does, so its firmware may be older than the API: install a newer one from the portal"
		about, err := client.About(ctx)
		switch {
		case errors.Is(err, api.ErrCertificateDates):
			// Its own root, with a clock wrong somewhere: no matter of its firmware.
		case err != nil:
			err = fmt.Errorf("%s: %w", older, err)
		case about.API < 1:
			err = errors.New(older)
		default:
			asked, err = client.RequestPairing(ctx, ClientLabel(host))
		}
		a.post(func() {
			p.busy = false
			if generation != p.pairing.Load() {
				client.Close()
				return
			}
			if err != nil {
				client.Close()
				var failure *api.Error
				if errors.As(err, &failure) && failure.RetryAfter > 0 {
					a.reportError(fmt.Errorf("%w You can ask again in %d s", err, int(failure.RetryAfter.Seconds())))
					return
				}
				a.reportError(err)
				return
			}
			a.report("")
			p.code = asked.Code
			p.seconds = asked.Seconds
			p.stage = stageCode
			go p.waitForApproval(a, client, generation, address, asked)
		})
	}()
}

func (p *connectPage) waitForApproval(a *App, client *api.Client, generation uint64, address string, asked api.PairRequest) {
	// The garland has few sockets, and an idle one left open would hold one for half a minute.
	approved := false
	defer func() {
		if !approved {
			client.Close()
		}
	}()
	deadline := time.Now().Add(time.Duration(asked.Seconds) * time.Second)
	for {
		select {
		case <-a.lifetime.Done():
			return
		case <-time.After(pairPollEvery):
		}
		ctx, cancel := context.WithTimeout(a.lifetime, requestLimit)
		answer, err := client.WaitPairing(ctx, asked.Request)
		cancel()
		if generation != p.pairing.Load() {
			return
		}
		var refused *api.Error
		switch {
		case err != nil && !errors.As(err, &refused) && time.Now().Before(deadline):
			// A weak link for a moment, while the owner walks to the buttons and the code still stands.
			continue
		case err != nil:
			a.post(func() {
				if generation == p.pairing.Load() {
					a.reportError(err)
					p.start(a)
				}
			})
			return
		case answer.State == api.PairApproved:
			approved = true
			a.post(func() {
				if generation == p.pairing.Load() {
					p.finish(a, client, address, answer.Token)
				} else {
					client.Close()
				}
			})
			return
		case answer.State != api.PairWaiting:
			a.post(func() {
				if generation == p.pairing.Load() {
					a.reportError(errors.New("the request ended without an answer: ask again"))
					p.start(a)
				}
			})
			return
		}
	}
}

func (p *connectPage) finish(a *App, client *api.Client, address, token string) {
	// Another garland met at this address stays: its own may have changed, and it is still paired.
	// One whose root is gone for good is the owner's to forget, from the list.
	fingerprint := p.root.Fingerprint
	name := p.name
	if name == "" {
		name = address
	}
	a.current = settings.Garland{Name: name, Address: address, RootPEM: string(p.root.PEM), Fingerprint: p.root.Fingerprint}
	savedErr := a.noteUsed()
	p.stage = stageAddress
	p.busy = false
	p.name = ""
	p.pairing.Add(1)
	a.useClient(client.WithToken(token))
	a.report("Paired.")
	// Last, so that what the footer is left saying is the worst of it.
	if savedErr != nil {
		a.reportError(fmt.Errorf("paired, but this garland could not be remembered: %w", savedErr))
	}
	// Kept away from the window, as a locked store may ask for its password first; a failure comes last.
	a.keyring(func() func() {
		err := tokenSet(fingerprint, token)
		return func() {
			if err != nil {
				a.reportError(fmt.Errorf("paired, but the token could not be kept, so pair again next time: %w", err))
			}
			p.refreshPaired(a)
		}
	})
}

func (p *connectPage) codeCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.back.Clicked(gtx) {
			p.start(a)
		}
		code := a.label(sizeTitle, colorStrong, p.code)
		return a.card(gtx, "Allow this program",
			code.Layout,
			a.hint(fmt.Sprintf("If the garland's screen shows this same code, press POWER and SPEED together there, within %d s. If it shows another code, someone else is asking: press any one button to refuse.", p.seconds)),
			func(gtx C) D { return a.labelButton(gtx, &p.back, buttonOther, "close", "Cancel", "", true) },
		)
	}
}
