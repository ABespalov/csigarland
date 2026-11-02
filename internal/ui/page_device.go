// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Device page: the LED count and the screen. The Network page: Wi-Fi, name, domain.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"

	"gioui.org/layout"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
)

type devicePage struct {
	mode         choice
	count        widget.Editor
	saveLeds     widget.Clickable
	saver        choice
	screen       widget.Editor
	delay        widget.Editor
	saverLevel   widget.Editor
	saveScreen   widget.Clickable
	last         api.Display
	savingScreen bool
}

func (p *devicePage) init() {
	p.count = newField(true)
	p.screen = newField(true)
	p.delay = newField(true)
	p.saverLevel = newField(true)
}

func (p *devicePage) load(a *App) {
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		state, err := client.State(ctx)
		if err != nil {
			return nil, err
		}
		display, err := client.Display(ctx)
		if err != nil {
			return nil, err
		}
		return func() {
			a.takeState(state)
			p.mode.Value = "auto"
			if state.Leds.Setting != 0 {
				p.mode.Value = "manual"
			}
			p.count.SetText(strconv.Itoa(state.Leds.Count))
			p.takeDisplay(display)
		}, nil
	})
}

func (p *devicePage) takeDisplay(display api.Display) {
	p.last = display
	p.saver.Value = display.Saver
	p.screen.SetText(strconv.Itoa(display.ScreenBrightness))
	p.delay.SetText(strconv.Itoa(display.Delay))
	p.saverLevel.SetText(strconv.Itoa(display.SaverBrightness))
}

// LedsText says what the garland is using, as the portal does.
func LedsText(leds api.Leds) string {
	switch {
	case leds.Kept != 0:
		return fmt.Sprintf("Could not change to %d LEDs. Still sending to %d: restart the garland, or choose another number.", leds.Count, leds.Kept)
	case leds.Setting != 0:
		return fmt.Sprintf("Using %d LEDs, set manually.", leds.Count)
	case leds.Probe == "present" && leds.Estimate > 0:
		return fmt.Sprintf("Using about %d LEDs, as detected.", leds.Count)
	case leds.Probe == "present":
		return fmt.Sprintf("LEDs found, but their number could not be measured. Using %d.", leds.Count)
	case leds.Probe == "absent":
		return fmt.Sprintf("No LEDs detected. Using %d.", leds.Count)
	default:
		return fmt.Sprintf("Checking for LEDs. Using %d.", leds.Count)
	}
}

func number(editor *widget.Editor, name string) (int, error) {
	value, err := strconv.Atoi(strings.TrimSpace(editor.Text()))
	if err != nil {
		return 0, fmt.Errorf("%s must be a number", name)
	}
	return value, nil
}

func (p *devicePage) rows(a *App) []layout.Widget {
	return []layout.Widget{p.ledsCard(a), p.screenCard(a)}
}

func (p *devicePage) ledsCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.saveLeds.Clicked(gtx) {
			p.saveCount(a)
		}
		rows := []layout.Widget{
			a.hint(LedsText(a.state.Leds)),
			// The count is asked for on the same line, only when it is set by hand.
			func(gtx C) D {
				children := []layout.FlexChild{layout.Rigid(func(gtx C) D { return a.segments(gtx, &p.mode, ledModes) })}
				if p.mode.Value == "manual" {
					children = append(children,
						layout.Rigid(layout.Spacer{Width: gapLarge}.Layout),
						layout.Rigid(func(gtx C) D { return a.field(gtx, &p.count, "32", 90) }))
				}
				return layout.Flex{Alignment: layout.Middle}.Layout(gtx, children...)
			},
		}
		rows = append(rows, func(gtx C) D { return a.labelButton(gtx, &p.saveLeds, buttonMain, "save", "Save", "", true) })
		return a.card(gtx, "LED count", rows...)
	}
}

func (p *devicePage) saveCount(a *App) {
	count := 0
	if p.mode.Value == "manual" {
		value, err := number(&p.count, "the LED count")
		if err != nil {
			a.reportError(err)
			return
		}
		count = value
	}
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		state, err := client.SetLeds(ctx, count)
		if err != nil {
			return nil, fmt.Errorf("could not save: %w", err)
		}
		return func() {
			a.takeState(state)
			a.report("Saved.")
		}, nil
	})
}

var ledModes = []segment{{"auto", "Detect automatically"}, {"manual", "Set manually"}}

var savers = []segment{
	{"none", "None"},
	{"starfield", "Starfield"},
	{"flying-clock", "Flying Clock"},
	{"breathing-dot", "Breathing Dot"},
	{"black-screen", "Black Screen"},
}

func (p *devicePage) screenCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.savingScreen {
			gtx = gtx.Disabled()
		}
		if p.saveScreen.Clicked(gtx) {
			p.saveDisplay(a)
		}
		rows := []layout.Widget{
			a.hint("The screensaver starts when no button has been pressed for this long. Any button wakes the screen."),
			a.hint("Screen brightness, %"),
			func(gtx C) D { return a.field(gtx, &p.screen, "100", 90) },
			a.hint("Screensaver"),
			func(gtx C) D { return a.segments(gtx, &p.saver, savers) },
		}
		if p.saver.Value != "none" {
			rows = append(rows, a.hint("Delay, seconds"), func(gtx C) D { return a.field(gtx, &p.delay, "60", 90) })
		}
		if p.saver.Value != "none" && p.saver.Value != "black-screen" {
			rows = append(rows, a.hint("Brightness during the screensaver, %"), func(gtx C) D { return a.field(gtx, &p.saverLevel, "30", 90) })
		}
		rows = append(rows, func(gtx C) D { return a.labelButton(gtx, &p.saveScreen, buttonMain, "save", "Save", "", true) })
		return a.card(gtx, "Screen", rows...)
	}
}

// readDisplay is the form as it stands. What is not shown for this screensaver is not read, as nobody
// could see what is wrong with it: it stays as the garland last said.
func (p *devicePage) readDisplay() (api.Display, error) {
	wanted := p.last
	wanted.Saver = p.saver.Value
	var err error
	if wanted.ScreenBrightness, err = number(&p.screen, "the screen brightness"); err != nil {
		return api.Display{}, err
	}
	if wanted.Saver != "none" {
		if wanted.Delay, err = number(&p.delay, "the delay"); err != nil {
			return api.Display{}, err
		}
	}
	if wanted.Saver != "none" && wanted.Saver != "black-screen" {
		if wanted.SaverBrightness, err = number(&p.saverLevel, "the screensaver's brightness"); err != nil {
			return api.Display{}, err
		}
	}
	return wanted, nil
}

func (p *devicePage) saveDisplay(a *App) {
	wanted, err := p.readDisplay()
	if err != nil {
		a.reportError(err)
		return
	}
	// The form holds still until the answer, which would otherwise replace what was typed since.
	p.savingScreen = true
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		display, err := client.SetDisplay(ctx, wanted)
		if err != nil {
			return func() { p.savingScreen = false }, fmt.Errorf("could not save: %w", err)
		}
		return func() {
			p.savingScreen = false
			p.takeDisplay(display)
			a.report("Saved.")
		}, nil
	})
}

const (
	scanPollEvery = time.Second
	scanGiveUp    = 30 * time.Second
	networksShown = 12
)

type networkPage struct {
	wifi       api.Wifi
	rootText   string
	scan       widget.Clickable
	scanning   bool
	found      []api.Network
	picks      [networksShown]widget.Clickable
	ssid       widget.Editor
	password   widget.Editor
	join       widget.Clickable
	forget     confirmClick
	name       widget.Editor
	saveName   widget.Clickable
	domain     widget.Editor
	saveDomain widget.Clickable
	copyRoot   widget.Clickable
}

func (p *networkPage) init() {
	p.ssid = newField(true)
	p.password = newField(true)
	p.password.Mask = '*'
	p.name = newField(true)
	p.domain = newField(true)
}

func (p *networkPage) load(a *App) {
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		wifi, err := client.Wifi(ctx)
		if err != nil {
			return nil, err
		}
		info, err := client.Info(ctx)
		if err != nil {
			return nil, err
		}
		return func() {
			p.take(wifi, info)
		}, nil
	})
}

// As the garland has them now: whatever these say is what a press of Save or Connect would send.
func (p *networkPage) take(wifi api.Wifi, info api.Info) {
	p.wifi = wifi
	p.rootText = info.RootFingerprint
	if wifi.Configured {
		p.ssid.SetText(wifi.SSID)
	}
	p.name.SetText(strings.TrimSuffix(wifi.Hostname, ".local"))
	p.domain.SetText(info.Domain)
}

// WifiText says how the home network stands, as the portal does.
func WifiText(wifi api.Wifi) string {
	switch {
	case wifi.Connected:
		return fmt.Sprintf("Connected to %s, as %s and %s.", wifi.SSID, wifi.Hostname, wifi.IP)
	case wifi.Configured:
		return fmt.Sprintf("Connecting to %s...", wifi.SSID)
	default:
		return "Not connected to a home network."
	}
}

func (p *networkPage) rows(a *App) []layout.Widget {
	return []layout.Widget{p.wifiCard(a), p.nameCard(a), p.certificateCard(a)}
}

func (p *networkPage) wifiCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.scan.Clicked(gtx) && !p.scanning {
			p.startScan(a)
		}
		if p.join.Clicked(gtx) {
			p.joinNetwork(a, p.ssid.Text(), p.password.Text())
		}
		if p.forget.confirmed(gtx) {
			p.joinNetwork(a, "", "")
		}
		rows := []layout.Widget{
			a.hint(WifiText(p.wifi)),
			func(gtx C) D {
				return a.labelButton(gtx, &p.scan, buttonOther, "search", "Find networks", "", !p.scanning)
			},
		}
		picks := make([]layout.Widget, 0, min(len(p.found), networksShown))
		for i := range min(len(p.found), networksShown) {
			picks = append(picks, func(gtx C) D {
				network := p.found[i]
				if p.picks[i].Clicked(gtx) {
					p.ssid.SetText(network.SSID)
					p.password.SetText("")
				}
				words := fmt.Sprintf("%s, %d dBm", network.SSID, network.RSSI)
				if network.Open {
					words += ", open"
				}
				return a.button(gtx, &p.picks[i], buttonOther, words, "Use this network", true)
			})
		}
		if len(picks) > 0 {
			rows = append(rows, func(gtx C) D { return row(gtx, picks...) })
		}
		rows = append(rows,
			a.hint("Network"),
			func(gtx C) D { return a.field(gtx, &p.ssid, "Name", 0) },
			a.hint("Password. Left empty for the network already saved, it keeps the one it has."),
			func(gtx C) D { return a.field(gtx, &p.password, "Password", 0) },
			func(gtx C) D {
				return row(gtx,
					func(gtx C) D {
						return a.labelButton(gtx, &p.join, buttonMain, "wifi", "Connect", "", p.ssid.Text() != "")
					},
					func(gtx C) D {
						return a.confirmLabelButton(gtx, &p.forget, buttonLosing, "unlink", "Forget", "Forget this network", p.wifi.Configured)
					},
				)
			},
		)
		return a.card(gtx, "Wi-Fi", rows...)
	}
}

func (p *networkPage) startScan(a *App) {
	p.scanning = true
	a.report("Searching, this takes a few seconds...")
	a.aside(func(ctx context.Context, client *api.Client) (func(), error) {
		scan, err := client.StartScan(ctx)
		deadline := time.Now().Add(scanGiveUp)
		for err == nil && scan.State == api.ScanRunning {
			if time.Now().After(deadline) {
				err = errors.New("no answer from the garland")
				break
			}
			select {
			case <-ctx.Done():
				err = ctx.Err()
			case <-time.After(scanPollEvery):
				scan, err = client.Scan(ctx)
			}
		}
		if err == nil && scan.State == api.ScanFailed {
			err = errors.New("the radio stayed busy, try again")
		}
		if err != nil {
			return func() { p.scanning = false }, fmt.Errorf("search failed: %w", err)
		}
		return func() {
			p.scanning = false
			p.found = scan.Networks
			a.report("Choose a network, or type its name.")
			if len(scan.Networks) == 0 {
				a.report("No networks found. You can still type the name.")
			}
		}, nil
	})
}

// A garland that takes one of these may leave the network or restart before it answers, so no answer
// is not a refusal.
func mayHaveTaken(err error) error {
	var refused *api.Error
	if errors.As(err, &refused) || errors.Is(err, api.ErrWrongRoot) || errors.Is(err, api.ErrCertificateDates) || errors.Is(err, context.Canceled) {
		return err
	}
	return fmt.Errorf("no answer, though the garland may have taken this and be restarting or changing network: %w", err)
}

func (p *networkPage) joinNetwork(a *App, ssid, password string) {
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		wifi, err := client.JoinWifi(ctx, ssid, password)
		if err != nil {
			return nil, mayHaveTaken(err)
		}
		return func() {
			p.wifi = wifi
			p.password.SetText("")
			a.report("The garland is joining the network. If its address changes, connect again.")
		}, nil
	})
}

func (p *networkPage) nameCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.saveName.Clicked(gtx) {
			name := strings.TrimSpace(p.name.Text())
			a.work(func(ctx context.Context, client *api.Client) (func(), error) {
				if err := client.SetName(ctx, name); err != nil {
					return nil, mayHaveTaken(err)
				}
				return func() { a.report("Saved. The garland restarts if the name changed.") }, nil
			})
		}
		return a.card(gtx, "mDNS name",
			a.hint("Lowercase letters, digits and hyphens, without .local. The garland restarts to take a new one."),
			func(gtx C) D { return a.field(gtx, &p.name, "csigarland", 0) },
			func(gtx C) D {
				return a.labelButton(gtx, &p.saveName, buttonMain, "save", "Save and restart", "", true)
			},
		)
	}
}

func (p *networkPage) certificateCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.saveDomain.Clicked(gtx) {
			domain := strings.TrimSpace(p.domain.Text())
			a.work(func(ctx context.Context, client *api.Client) (func(), error) {
				if err := client.SetDomain(ctx, domain); err != nil {
					return nil, mayHaveTaken(err)
				}
				return func() { a.report("Saved. The garland restarts with new certificates.") }, nil
			})
		}
		return a.card(gtx, "HTTPS certificate",
			a.copyFact("Root SHA-256", p.rootText, &p.copyRoot),
			a.hint("Your domain or fixed IP address, if the garland is reached by one. A change makes a new root: browsers need it installed again, and this program connects and pairs again."),
			func(gtx C) D { return a.field(gtx, &p.domain, "garland.example.com or 192.168.1.50", 0) },
			func(gtx C) D {
				return a.labelButton(gtx, &p.saveDomain, buttonMain, "save", "Save and restart", "", true)
			},
		)
	}
}
