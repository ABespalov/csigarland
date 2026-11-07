// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: What opens over a page: the frame of a modal window, and an effect's preview here.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"image"
	"image/color"
	"math"
	"sync"
	"time"

	"gioui.org/io/event"
	"gioui.org/io/key"
	"gioui.org/io/pointer"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/firmware/app/effects"
	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/engine"
)

type modalKind int

const (
	modalNone modalKind = iota
	modalTune
	modalPreview
	modalOffer
)

const (
	previewLeds   = 32 // With no garland connected, the owner's: 8 figurines, 4 of each.
	previewFrame  = 33 * time.Millisecond
	dimmedOpacity = 0xB0
)

// chainLeds is how many LEDs the preview and Tune draw, and work the current out for: as many as the garland
// connected to sends to, as its Device page sets or its probe finds, and previewLeds with none.
func (a *App) chainLeds() int {
	leds := a.state.Leds.Count
	if a.state.Leds.Kept != 0 {
		leds = a.state.Leds.Kept
	}
	if a.client == nil || leds <= 0 {
		return previewLeds
	}
	return min(leds, engine.MaxLeds)
}

// fitFrame is frame for a chain of leds, made anew when the count changed.
func fitFrame(frame []engine.Color, leds int) []engine.Color {
	if len(frame) == leds {
		return frame
	}
	return make([]engine.Color, leds)
}

// stripText says what the strip shows, on how many LEDs.
func stripText(leds int) string {
	return fmt.Sprintf("Drawn by the firmware's own engine, on %d LEDs numbered from 0 at the board along the cable, as start and stop count them. Figurines soften light in a way a screen does not show, so judge the result on the garland.", leds)
}

var library = sync.OnceValues(effects.Library)

// libraryEffects are the built-in effects as the Effects page lists them.
func libraryEffects() []api.Effect {
	found, err := library()
	if err != nil {
		return nil
	}
	list := make([]api.Effect, len(found))
	for i, effect := range found {
		list[i] = api.Effect{ID: effect.ID, Name: effect.Name, Group: effect.Group, BuiltIn: true}
	}
	return list
}

func libraryFile(id string) ([]byte, bool) {
	found, err := library()
	if err != nil {
		return nil, false
	}
	for _, effect := range found {
		if effect.ID == id {
			return effect.File, true
		}
	}
	return nil, false
}

// withEffectFile gets an effect's file from where the list on the page came from: the garland, or
// the built-in ones when that list is showing. A garland that fails here says so, rather than have
// the built-in file stand in for one that may have been replaced on it.
func (a *App) withEffectFile(id string, then func(file []byte)) {
	a.modalAsked++
	asked := a.modalAsked
	if a.client == nil || a.effects.showsLibrary(a) {
		if file, known := libraryFile(id); known {
			then(file)
			return
		}
		a.reportError(errors.New("this effect is not among the built-in ones"))
		return
	}
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		file, err := client.EffectFile(ctx, id)
		if err != nil {
			return nil, err
		}
		return func() {
			// Asked for again since, or a window was opened another way, with a draft perhaps in it.
			if asked == a.modalAsked {
				then(file)
			}
		}, nil
	})
}

// The preview's speed and brightness start as the garland's.
func (a *App) openModal(kind modalKind) {
	a.modalAsked++
	a.modal = kind
	switch kind {
	case modalTune:
		a.tune.look.seed(a.state)
		a.readGarlandPower()
	case modalPreview:
		a.preview.look.seed(a.state)
	}
	a.status = ""
	a.failed = false
}

// Escape does as the window's close button does.
func (a *App) closeModal(now time.Time) {
	if a.modal == modalTune {
		if !a.tune.mayClose(now) {
			return
		}
		a.tune.take("", nil)
	}
	a.modal = modalNone
	a.status = ""
	a.failed = false
}

func (a *App) layoutModal(gtx C) {
	for {
		seen, ok := gtx.Event(key.Filter{Name: key.NameEscape})
		if !ok {
			break
		}
		if pressed, isKey := seen.(key.Event); isKey && pressed.State == key.Press {
			// The innermost window first: the colour picker over Tune, or a page's own.
			switch {
			case a.modal == modalTune && a.tune.colors.open:
				a.tune.colors.open = false
			case a.modal == modalNone:
				a.schedule.popup = popupNone
			default:
				a.closeModal(gtx.Now)
			}
		}
	}
	if a.modal == modalNone {
		// The Schedule page's windows, for a time or an effect, over it alone and not the journal.
		if a.pageShown() && a.page == PageSchedule {
			if body := a.schedule.popupWindow(a); body != nil {
				backdrop(gtx, &a.schedule.popup, dimmedOpacity)
				a.modalCard(gtx, false, false, body)
			}
		}
		return
	}
	backdrop(gtx, &a.modal, dimmedOpacity)
	switch a.modal {
	case modalTune:
		a.modalCard(gtx, true, true, a.tune.body(a))
		// Lighter, so the LEDs above it still show what the colour does.
		if a.tune.colors.open {
			backdrop(gtx, &a.tune.colors, pickerDimmed)
			a.modalCard(gtx, false, false, a.tune.pickerWindow(a))
		}
	case modalPreview:
		a.modalCard(gtx, true, false, a.preview.body(a))
	case modalOffer:
		a.modalCard(gtx, false, false, a.offer.body(a))
	}
}

const pickerDimmed = 0x60

// backdrop dims what is under a window, and takes the pointer from it: a button there is not hovered
// by a pointer over the window.
func backdrop(gtx C, tag event.Tag, opacity uint8) {
	area := clip.Rect{Max: gtx.Constraints.Max}.Push(gtx.Ops)
	paint.Fill(gtx.Ops, color.NRGBA{A: opacity})
	for {
		if _, ok := gtx.Event(pointer.Filter{Target: tag, Kinds: pointer.Press | pointer.Release | pointer.Move | pointer.Drag | pointer.Scroll,
			ScrollX: pointer.ScrollRange{Min: math.MinInt32, Max: math.MaxInt32}, ScrollY: pointer.ScrollRange{Min: math.MinInt32, Max: math.MaxInt32}}); !ok {
			break
		}
	}
	event.Op(gtx.Ops, tag)
	area.Pop()
}

// A panel at the middle of the window: as wide as the window if wide says so, for the LEDs to have
// the room, or else as its contents, and as tall as the window if tall says so.
func (a *App) modalCard(gtx C, wide, tall bool, body layout.Widget) {
	margin := gtx.Dp(gapLarge)
	pad := gtx.Dp(gapLarge)
	inner := gtx
	inner.Constraints.Max.X = gtx.Constraints.Max.X - 2*margin - 2*pad
	inner.Constraints.Max.Y = gtx.Constraints.Max.Y - 2*margin - 2*pad
	inner.Constraints.Min = image.Point{}
	if wide {
		inner.Constraints.Min.X = inner.Constraints.Max.X
	}
	if tall {
		inner.Constraints.Min.Y = inner.Constraints.Max.Y
	}
	macro := op.Record(gtx.Ops)
	dims := body(inner)
	drawn := macro.Stop()
	size := dims.Size.Add(image.Pt(2*pad, 2*pad))
	place := image.Pt((gtx.Constraints.Max.X-size.X)/2, (gtx.Constraints.Max.Y-size.Y)/2)
	moved := op.Offset(place).Push(gtx.Ops)
	fill(gtx, size, gtx.Dp(corner), colorPanel)
	inset := op.Offset(image.Pt(pad, pad)).Push(gtx.Ops)
	drawn.Add(gtx.Ops)
	inset.Pop()
	moved.Pop()
}

// The window's title, with what closes it at the end of the line.
func (a *App) modalTitle(gtx C, title string, tools ...layout.Widget) D {
	children := []layout.FlexChild{layout.Flexed(1, a.heading(title))}
	for _, tool := range tools {
		children = append(children, layout.Rigid(layout.Spacer{Width: gap}.Layout), layout.Rigid(tool))
	}
	return layout.Flex{Alignment: layout.Middle}.Layout(gtx, children...)
}

// What the window underneath would say, as it is dimmed.
func (a *App) modalStatus(gtx C) D {
	if a.status == "" {
		return D{}
	}
	return layout.Inset{Top: gap}.Layout(gtx, a.statusLine(a.status, a.failed))
}

// previewWindow plays one effect here, by the firmware's own engine, and changes nothing anywhere.
type previewWindow struct {
	id      string
	name    string
	file    []byte
	preview *engine.Preview
	look    previewLook
	frame   []engine.Color
	close   widget.Clickable
	tune    widget.Clickable
}

func (p *previewWindow) init() {
	p.frame = make([]engine.Color, previewLeds)
}

func (p *previewWindow) open(a *App, id, name string) {
	a.withEffectFile(id, func(file []byte) {
		preview, err := engine.NewPreview(file)
		if err != nil {
			a.reportError(err)
			return
		}
		p.id, p.name, p.file, p.preview = id, name, file, preview
		p.look.clock.restart()
		a.openModal(modalPreview)
	})
}

func (p *previewWindow) body(a *App) layout.Widget {
	return func(gtx C) D {
		if p.close.Clicked(gtx) {
			a.closeModal(gtx.Now)
		}
		if p.tune.Clicked(gtx) {
			a.tune.take(p.id, p.file)
			a.openModal(modalTune)
			a.tune.look.takeFrom(&p.look)
		}
		return a.column(gtx,
			func(gtx C) D {
				return a.modalTitle(gtx, p.name,
					func(gtx C) D {
						return a.labelButton(gtx, &p.tune, buttonOther, "tune", "Tune", "Tune this effect", true)
					},
					func(gtx C) D { return a.labelButton(gtx, &p.close, buttonOther, "close", "Close", "", true) },
				)
			},
			func(gtx C) D {
				p.frame = fitFrame(p.frame, a.chainLeds())
				return a.layoutStrip(gtx, p.preview, &p.look, p.frame)
			},
			a.lookSliders(&p.look),
			a.hint(stripText(a.chainLeds())),
		)
	}
}
