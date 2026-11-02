// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Picking a colour: a square of saturation and value, a strip of hue, and its hex.
// Contributors: None

package ui

import (
	"image"
	"image/color"
	"math"

	"gioui.org/f32"
	"gioui.org/gesture"
	"gioui.org/io/pointer"
	"gioui.org/layout"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget"
)

const (
	pickerSquareWide = unit.Dp(240)
	pickerSquareHigh = unit.Dp(140)
	pickerHueHigh    = unit.Dp(16)
	pickerMark       = unit.Dp(6)
	swatchSize       = unit.Dp(28)
)

// colorEditor is the picker, in a window over Tune: which colour it is on, and how it is moved.
type colorEditor struct {
	open bool
	at   FileColor // Which colour: its Layer, Field and Index.
	// Kept apart from the colour, as a grey or a black has no hue of its own to come back to.
	hsv    HSV
	shown  RGB
	square gesture.Drag
	hue    gesture.Drag
	hex    widget.Editor
	done   widget.Clickable
	remove confirmClick
}

func (e *colorEditor) choose(at FileColor) {
	e.open, e.at = true, at
	e.hsv, e.shown = at.Color.ToHSV(), at.Color
}

func nrgba(c RGB) color.NRGBA { return color.NRGBA{R: c.R, G: c.G, B: c.B, A: 0xFF} }

// colorPicker draws the picker for a colour as the file has it now, and calls set with a new one.
func (a *App) colorPicker(gtx C, e *colorEditor, current RGB, set func(RGB)) D {
	if current != e.shown {
		e.hsv, e.shown = current.ToHSV(), current
	}
	wide, high := gtx.Dp(pickerSquareWide), gtx.Dp(pickerSquareHigh)
	hueHigh := gtx.Dp(pickerHueHigh)
	take := func(next HSV) {
		e.hsv = next
		rgb := next.ToRGB()
		e.shown = rgb
		if rgb != current {
			set(rgb)
		}
	}
	for {
		drag, ok := e.square.Update(gtx.Metric, gtx.Source, gesture.Both)
		if !ok {
			break
		}
		if drag.Kind == pointer.Press || drag.Kind == pointer.Drag {
			next := e.hsv
			next.S = clamp01(float64(drag.Position.X) / float64(wide))
			next.V = 1 - clamp01(float64(drag.Position.Y)/float64(high))
			take(next)
		}
	}
	for {
		drag, ok := e.hue.Update(gtx.Metric, gtx.Source, gesture.Horizontal)
		if !ok {
			break
		}
		if drag.Kind == pointer.Press || drag.Kind == pointer.Drag {
			next := e.hsv
			next.H = clamp01(float64(drag.Position.X)/float64(wide)) * 359.99
			take(next)
		}
	}
	e.hex.SingleLine = true
	if !gtx.Focused(&e.hex) && e.hex.Text() != e.shown.Hex() {
		e.hex.SetText(e.shown.Hex())
	}
	for {
		seen, ok := e.hex.Update(gtx)
		if !ok {
			break
		}
		if _, changed := seen.(widget.ChangeEvent); changed {
			if typed, valid := ParseHex(e.hex.Text()); valid && typed != current {
				e.hsv, e.shown = typed.ToHSV(), typed
				set(typed)
			}
		}
	}
	square := func(gtx C) D {
		a.drawSquare(gtx, e, wide, high)
		return D{Size: image.Pt(wide, high)}
	}
	strip := func(gtx C) D {
		a.drawHueStrip(gtx, e, wide, hueHigh)
		return D{Size: image.Pt(wide, hueHigh)}
	}
	side := func(gtx C) D {
		return a.column(gtx,
			func(gtx C) D { return swatch(gtx, e.shown, true) },
			a.hint("Hex"),
			func(gtx C) D { return a.field(gtx, &e.hex, "FF8800", 110) },
		)
	}
	return layout.Flex{}.Layout(gtx,
		layout.Rigid(func(gtx C) D { return a.column(gtx, square, strip) }),
		layout.Rigid(layout.Spacer{Width: gapLarge}.Layout),
		layout.Rigid(side),
	)
}

func clamp01(v float64) float64 { return max(0, min(1, v)) }

// Full colour at the hue, washed to white to the left and darkened to black downwards.
func (a *App) drawSquare(gtx C, e *colorEditor, wide, high int) {
	area := clip.Rect{Max: image.Pt(wide, high)}.Push(gtx.Ops)
	paint.Fill(gtx.Ops, nrgba(HSV{H: e.hsv.H, S: 1, V: 1}.ToRGB()))
	paint.LinearGradientOp{Stop1: f32.Pt(0, 0), Color1: color.NRGBA{R: 255, G: 255, B: 255, A: 255},
		Stop2: f32.Pt(float32(wide), 0), Color2: color.NRGBA{R: 255, G: 255, B: 255}}.Add(gtx.Ops)
	paint.PaintOp{}.Add(gtx.Ops)
	paint.LinearGradientOp{Stop1: f32.Pt(0, 0), Color1: color.NRGBA{},
		Stop2: f32.Pt(0, float32(high)), Color2: color.NRGBA{A: 255}}.Add(gtx.Ops)
	paint.PaintOp{}.Add(gtx.Ops)
	e.square.Add(gtx.Ops)
	pointer.CursorCrosshair.Add(gtx.Ops)
	area.Pop()
	at := f32.Pt(float32(e.hsv.S)*float32(wide), float32(1-e.hsv.V)*float32(high))
	ring(gtx, at, float32(gtx.Dp(pickerMark)), color.NRGBA{R: 255, G: 255, B: 255, A: 255})
}

// The hues in order, red to red, with a mark at this one.
func (a *App) drawHueStrip(gtx C, e *colorEditor, wide, high int) {
	area := clip.Rect{Max: image.Pt(wide, high)}.Push(gtx.Ops)
	const parts = 6
	for i := range parts {
		left, right := wide*i/parts, wide*(i+1)/parts
		band := clip.Rect{Min: image.Pt(left, 0), Max: image.Pt(right, high)}.Push(gtx.Ops)
		paint.LinearGradientOp{
			Stop1: f32.Pt(float32(left), 0), Color1: nrgba(HSV{H: float64(i) * 60, S: 1, V: 1}.ToRGB()),
			Stop2: f32.Pt(float32(right), 0), Color2: nrgba(HSV{H: float64(i+1) * 60, S: 1, V: 1}.ToRGB()),
		}.Add(gtx.Ops)
		paint.PaintOp{}.Add(gtx.Ops)
		band.Pop()
	}
	e.hue.Add(gtx.Ops)
	area.Pop()
	x := int(e.hsv.H / 360 * float64(wide))
	mark := max(gtx.Dp(unit.Dp(2)), 1)
	paint.FillShape(gtx.Ops, color.NRGBA{R: 255, G: 255, B: 255, A: 255},
		clip.Rect{Min: image.Pt(x-mark, -mark), Max: image.Pt(x+mark, high+mark)}.Op())
}

func ring(gtx C, at f32.Point, radius float32, ink color.NRGBA) {
	var path clip.Path
	path.Begin(gtx.Ops)
	path.MoveTo(f32.Pt(at.X+radius, at.Y))
	path.ArcTo(at, at, 2*math.Pi)
	paint.FillShape(gtx.Ops, ink, clip.Stroke{Path: path.End(), Width: 2 * gtx.Metric.PxPerDp}.Op())
}

// swatch is a colour in a rounded square, ringed when it is the one being picked.
func swatch(gtx C, c RGB, chosen bool) D {
	size := gtx.Dp(swatchSize)
	fill(gtx, image.Pt(size, size), gtx.Dp(buttonCorner), nrgba(c))
	edge := colorBorder
	width := unit.Dp(1)
	if chosen {
		edge, width = colorStrong, unit.Dp(2)
	}
	widget.Border{Color: edge, Width: width, CornerRadius: buttonCorner}.Layout(gtx, func(gtx C) D {
		return D{Size: image.Pt(size, size)}
	})
	return D{Size: image.Pt(size, size)}
}

// A swatch that is pressed to pick its colour.
func (a *App) swatchButton(gtx C, click *widget.Clickable, c RGB, chosen bool, tip string) D {
	over := a.hovered(gtx, click)
	if over {
		a.tipOffered = tip
	}
	dims := click.Layout(gtx, func(gtx C) D { return swatch(gtx, c, chosen || over) })
	a.watchHover(gtx, click, dims.Size)
	return dims
}
