// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: What stands around every page, after csiPass: top bar, rail, footer and journal.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"image"
	"image/color"
	"time"

	"gioui.org/font"
	"gioui.org/layout"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
)

// The wordmark in the csiPass 5x7 pixel face, the same on the garland's glass, the portal and the
// enclosure's lid: five column bytes per letter, bit 0 the top row. firmware/app/src/wordmark.h
// has them too, and a test holds the two alike.
var wordmarkGlyphs = [10][5]uint8{
	{56, 68, 68, 68, 32},  // c
	{72, 84, 84, 84, 32},  // s
	{0, 68, 125, 64, 0},   // i
	{62, 65, 73, 73, 122}, // G
	{32, 84, 84, 84, 120}, // a
	{124, 8, 4, 4, 8},     // r
	{0, 65, 127, 64, 0},   // l
	{32, 84, 84, 84, 120}, // a
	{124, 8, 4, 4, 120},   // n
	{56, 68, 68, 72, 127}, // d
}

// What stands under the wordmark, the portal's too, and what the footer says with nothing to report.
const (
	brandTagline = "color your weekdays"
	statusSlogan = "Any day can be a holiday."
)

const (
	wordmarkWhiteLetters = 3 // "csi" white, "Garland" blue.
	wordmarkPitch        = 6
	wordmarkRows         = 7
	wordmarkColumns      = len(wordmarkGlyphs)*wordmarkPitch - 1

	wordmarkCell = unit.Dp(4)
	topBarHeight = unit.Dp(64)
	navHeight    = unit.Dp(36)
	journalKept  = 200
)

// Whole pixels to a cell, so that it stays sharp at any scale.
func layoutWordmark(gtx C) D {
	cell := max(gtx.Dp(wordmarkCell), 1)
	for letter, glyph := range wordmarkGlyphs {
		ink := colorBrand
		if letter < wordmarkWhiteLetters {
			ink = colorStrong
		}
		for column, bits := range glyph {
			for row := range wordmarkRows {
				if bits&(1<<row) == 0 {
					continue
				}
				x := (letter*wordmarkPitch + column) * cell
				y := row * cell
				area := clip.Rect{Min: image.Pt(x, y), Max: image.Pt(x+cell, y+cell)}.Push(gtx.Ops)
				paint.Fill(gtx.Ops, ink)
				area.Pop()
			}
		}
	}
	return D{Size: image.Pt(wordmarkColumns*cell, wordmarkRows*cell)}
}

// The wordmark, with the tagline spread to just its width.
func (a *App) layoutBrand(gtx C) D {
	width := wordmarkColumns * max(gtx.Dp(wordmarkCell), 1)
	gtx.Constraints.Min = image.Point{} // Only as tall as it is, for the bar to centre it.
	return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
		layout.Rigid(layoutWordmark),
		layout.Rigid(layout.Spacer{Height: unit.Dp(3)}.Layout),
		layout.Rigid(func(gtx C) D {
			gtx.Constraints.Min.X = width
			gtx.Constraints.Max.X = width
			letters := make([]layout.FlexChild, 0, len(brandTagline))
			for i, letter := range brandTagline {
				words := string(letter)
				if letter == ' ' {
					words = "\u00a0"
				}
				letters = append(letters, layout.Rigid(a.label(sizeTagline, taglineInk(i), words).Layout))
			}
			return layout.Flex{Spacing: layout.SpaceBetween}.Layout(gtx, letters...)
		}),
	)
}

// A letter's place on the gradient the portal has too, from csi's white to Garland's blue over the line.
func taglineInk(letter int) color.NRGBA {
	between := func(from, to uint8) uint8 {
		return uint8(int(from) + (int(to)-int(from))*letter/(len(brandTagline)-1))
	}
	return color.NRGBA{R: between(colorStrong.R, colorBrand.R), G: between(colorStrong.G, colorBrand.G), B: between(colorStrong.B, colorBrand.B), A: 0xFF}
}

func (a *App) topBar(gtx C) D {
	height := gtx.Dp(topBarHeight)
	fill(gtx, image.Pt(gtx.Constraints.Max.X, height), 0, colorSurface)
	gtx.Constraints.Min = image.Pt(gtx.Constraints.Max.X, height)
	gtx.Constraints.Max.Y = height
	return layout.Inset{Left: gapPage, Right: gapPage}.Layout(gtx, func(gtx C) D {
		// Always there, as in the csiPass companion, even with little of it to go to.
		children := []layout.FlexChild{layout.Rigid(a.layoutBrand), layout.Flexed(1, layout.Spacer{}.Layout)}
		for i := range a.tabs {
			children = append(children, layout.Rigid(func(gtx C) D { return a.navItem(gtx, Page(i)) }))
		}
		return layout.Flex{Alignment: layout.Middle}.Layout(gtx, children...)
	})
}

func (a *App) navItem(gtx C, page Page) D {
	click := &a.tabs[page]
	available := a.pageAvailable(page)
	active := a.pageShown() && page == a.page
	over := a.hovered(gtx, click)
	if a.firmware.busy {
		gtx = gtx.Disabled()
	}
	if over && !available {
		a.tipOffered = "Needs a garland: connect to one first"
	}
	dims := click.Layout(gtx, func(gtx C) D {
		style := a.label(sizeBody, colorMuted, pageNames[page])
		style.Font.Weight = font.SemiBold
		lit := active || (available && over && !a.firmware.busy)
		switch {
		case lit:
			style.Color = colorText
		case a.firmware.busy || !available:
			style.Color = colorBorder
		}
		height := gtx.Dp(navHeight)
		return layout.Stack{Alignment: layout.Center}.Layout(gtx,
			layout.Expanded(func(gtx C) D {
				size := image.Pt(gtx.Constraints.Min.X, height)
				if lit {
					fill(gtx, size, gtx.Dp(unit.Dp(8)), colorQuiet)
				}
				if active {
					line := gtx.Dp(unit.Dp(2))
					bottom := clip.Rect{Min: image.Pt(0, height-line), Max: size}.Push(gtx.Ops)
					paint.Fill(gtx.Ops, colorBrand)
					bottom.Pop()
				}
				return D{Size: size}
			}),
			layout.Stacked(func(gtx C) D {
				gtx.Constraints.Min.Y = height
				return layout.Inset{Left: unit.Dp(9), Right: unit.Dp(9)}.Layout(gtx, func(gtx C) D {
					return layout.Center.Layout(gtx, style.Layout)
				})
			}),
		)
	})
	a.watchHover(gtx, click, dims.Size)
	return dims
}

// Whether the garland last answered, whatever it answered.
type onlineState int

const (
	stateChecking onlineState = iota
	stateOnline
	stateOffline
)

func (a *App) noteAnswer(err error) {
	var answered *api.Error
	switch {
	case err == nil || errors.As(err, &answered):
		a.online = stateOnline
	case !errors.Is(err, context.Canceled):
		a.online = stateOffline
	}
}

func (a *App) rail(gtx C) D {
	if a.rescan.Clicked(gtx) && a.connected && !a.firmware.busy {
		a.open(a.page)
	}
	if a.toConnect.Clicked(gtx) {
		a.leaveLocal()
	}
	return layout.Inset{Top: unit.Dp(10), Left: gapPage, Right: gapPage}.Layout(gtx, func(gtx C) D {
		gtx.Constraints.Min.X = gtx.Constraints.Max.X
		return layout.Stack{}.Layout(gtx,
			layout.Expanded(func(gtx C) D {
				radius := gtx.Dp(unit.Dp(10))
				fill(gtx, gtx.Constraints.Min, radius, colorPanel)
				return widget.Border{Color: colorQuiet, Width: unit.Dp(1), CornerRadius: unit.Dp(10)}.Layout(gtx, func(gtx C) D {
					return D{Size: gtx.Constraints.Min}
				})
			}),
			layout.Stacked(func(gtx C) D {
				return layout.Inset{Top: unit.Dp(9), Bottom: unit.Dp(9), Left: gapLarge, Right: unit.Dp(10)}.Layout(gtx, func(gtx C) D {
					gtx.Constraints.Min.X = gtx.Constraints.Max.X
					return a.railItems(gtx)
				})
			}),
		)
	})
}

func (a *App) railItems(gtx C) D {
	space := layout.Rigid(layout.Spacer{Width: unit.Dp(18)}.Layout)
	children := []layout.FlexChild{layout.Rigid(a.onlineItem)}
	if !a.connected && (a.local || a.journalOpen) {
		children = append(children, layout.Flexed(1, layout.Spacer{}.Layout),
			layout.Rigid(func(gtx C) D {
				return a.labelButton(gtx, &a.toConnect, buttonOther, "link", "Connect", "Connect to a garland", true)
			}))
	}
	if a.connected {
		children = append(children, space, layout.Rigid(a.railFact("Garland", a.current.Address)))
		if a.about.Version != "" {
			children = append(children, space, layout.Rigid(a.railFact("Firmware", a.about.Version)))
		}
		if a.about.OnTrial {
			children = append(children, layout.Rigid(layout.Spacer{Width: unit.Dp(6)}.Layout), layout.Rigid(a.chip("ON TRIAL", colorActive)))
		}
		children = append(children, space, layout.Flexed(1, a.railPower()))
		children = append(children, layout.Rigid(func(gtx C) D {
			return a.labelButton(gtx, &a.rescan, buttonOther, "refresh", "Refresh", "Read this page again", !a.firmware.busy)
		}))
		children = append(children, layout.Rigid(layout.Spacer{Width: gap}.Layout), layout.Rigid(func(gtx C) D {
			return a.labelButton(gtx, &a.leave, buttonOther, "unlink", "Disconnect", "Leave this garland for another, and stay paired with it", !a.firmware.busy)
		}))
	}
	return layout.Flex{Alignment: layout.Middle}.Layout(gtx, children...)
}

func (a *App) onlineItem(gtx C) D {
	ink, words := colorActive, "Checking"
	switch {
	case !a.connected || a.online == stateOffline:
		ink, words = colorMuted, "Offline"
	case a.online == stateOnline:
		ink, words = colorGood, "Online"
	}
	return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
		layout.Rigid(func(gtx C) D {
			size := gtx.Dp(unit.Dp(16))
			halo := color.NRGBA{R: ink.R, G: ink.G, B: ink.B, A: 0x30}
			paint.FillShape(gtx.Ops, halo, clip.Ellipse{Max: image.Pt(size, size)}.Op(gtx.Ops))
			inset := gtx.Dp(unit.Dp(4))
			paint.FillShape(gtx.Ops, ink, clip.Ellipse{Min: image.Pt(inset, inset), Max: image.Pt(size-inset, size-inset)}.Op(gtx.Ops))
			return D{Size: image.Pt(size, size)}
		}),
		layout.Rigid(layout.Spacer{Width: unit.Dp(7)}.Layout),
		layout.Rigid(a.body(words)),
	)
}

func (a *App) railFact(name, value string) layout.Widget {
	return func(gtx C) D {
		caption := a.label(sizeCaption, colorMuted, upperWords(name))
		caption.Font.Weight = font.SemiBold
		line := a.label(sizeBody, colorText, value)
		line.MaxLines = 1
		return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
			layout.Rigid(caption.Layout),
			layout.Rigid(layout.Spacer{Width: unit.Dp(7)}.Layout),
			layout.Rigid(line.Layout),
		)
	}
}

func (a *App) railPower() layout.Widget {
	words := "Off"
	if a.state.Power {
		words = a.state.EffectName
	}
	return func(gtx C) D {
		if a.state.Power && a.state.EffectName == "" {
			return D{}
		}
		return a.railFact("Playing", words)(gtx)
	}
}

func (a *App) chip(words string, ink color.NRGBA) layout.Widget {
	return func(gtx C) D {
		border := widget.Border{Color: ink, Width: unit.Dp(1), CornerRadius: unit.Dp(4)}
		return border.Layout(gtx, func(gtx C) D {
			return layout.Inset{Top: unit.Dp(2), Bottom: unit.Dp(2), Left: unit.Dp(6), Right: unit.Dp(6)}.Layout(gtx, a.label(sizeCaption, ink, words).Layout)
		})
	}
}

// One line, always this tall, and the way into the journal for what does not fit it.
func (a *App) footer(gtx C) D {
	if a.journalButton.Clicked(gtx) {
		a.journalOpen = !a.journalOpen
		a.journalUnseen = 0
	}
	return layout.Stack{}.Layout(gtx,
		layout.Expanded(func(gtx C) D {
			size := image.Pt(gtx.Constraints.Max.X, gtx.Constraints.Min.Y)
			fill(gtx, size, 0, colorSurface)
			return D{Size: size}
		}),
		layout.Stacked(func(gtx C) D {
			gtx.Constraints.Min.X = gtx.Constraints.Max.X
			return layout.Inset{Top: gapSmall, Bottom: gapSmall, Left: gapPage, Right: gapPage}.Layout(gtx, func(gtx C) D {
				words, tip, kind := "Journal", "", buttonOther
				switch {
				case a.journalOpen:
					words, kind = "Close journal", buttonMain
				case a.journalUnseen > 0:
					words, tip, kind = fmt.Sprintf("Journal (%d)", a.journalUnseen), fmt.Sprintf("%d errors not looked at", a.journalUnseen), buttonLosing
				}
				return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
					layout.Flexed(1, func(gtx C) D {
						gtx.Constraints.Min.X = gtx.Constraints.Max.X
						line := a.label(sizeSmall, statusInk(a.failed), a.status)
						if a.status == "" {
							line = a.label(sizeSmall, colorMuted, statusSlogan)
						}
						line.MaxLines = 1
						return line.Layout(gtx)
					}),
					layout.Rigid(layout.Spacer{Width: gap}.Layout),
					layout.Rigid(func(gtx C) D { return a.labelButton(gtx, &a.journalButton, kind, "list", words, tip, true) }),
					layout.Rigid(layout.Spacer{Width: unit.Dp(12)}.Layout),
					layout.Rigid(a.hint(a.version)),
				)
			})
		}),
	)
}

// Report is one line the footer has said.
type report struct {
	at     time.Time
	words  string
	failed bool
}

func (a *App) addReport(words string, failed bool) {
	// The same again, as a look every few seconds may say: the line already there is brought up to now.
	if last := len(a.journal) - 1; last >= 0 && a.journal[last].words == words && a.journal[last].failed == failed {
		a.journal[last].at = time.Now()
		return
	}
	a.journal = append(a.journal, report{at: time.Now(), words: words, failed: failed})
	if len(a.journal) > journalKept {
		a.journal = a.journal[len(a.journal)-journalKept:]
	}
	if failed && !a.journalOpen {
		a.journalUnseen++
	}
}

// Newest first. Everything the footer said, in full, for as long as the window is open.
func (a *App) journalRows() []layout.Widget {
	rows := []layout.Widget{}
	if len(a.journal) == 0 {
		rows = append(rows, a.hint("Nothing yet."))
	}
	for i := len(a.journal) - 1; i >= 0; i-- {
		entry := a.journal[i]
		rows = append(rows, func(gtx C) D {
			return layout.Flex{}.Layout(gtx,
				layout.Rigid(func(gtx C) D {
					gtx.Constraints.Min.X = gtx.Dp(unit.Dp(76))
					return a.hint(entry.at.Format("15:04:05"))(gtx)
				}),
				layout.Flexed(1, a.statusLine(entry.words, entry.failed)),
			)
		})
	}
	return rows
}
