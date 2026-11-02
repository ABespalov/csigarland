// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Buttons with a glyph, with their words or a tooltip for them, and the tooltips.
// Contributors: None

package ui

import (
	"fmt"
	"image"
	"image/color"
	"io"
	"strconv"
	"strings"
	"time"

	"gioui.org/f32"
	"gioui.org/gesture"
	"gioui.org/io/clipboard"
	"gioui.org/io/event"
	"gioui.org/io/pointer"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget"
)

const (
	glyphGrid       = 24
	glyphStroke     = 2
	iconButtonSize  = unit.Dp(36)
	glyphDrawnSize  = unit.Dp(20)
	labelGlyphSize  = unit.Dp(18)
	labelGlyphPad   = unit.Dp(9)
	labelGlyphGap   = unit.Dp(6)
	labelPad        = unit.Dp(12)
	buttonCorner    = unit.Dp(6)
	tipDelay        = 350 * time.Millisecond
	tipPad          = unit.Dp(6)
	tipFromPointer  = unit.Dp(16)
	disabledOpacity = 0x60
)

// One step of a glyph's path: a letter and its numbers.
type glyphStep struct {
	command byte
	points  []f32.Point
}

// All of them once, at the start, so that nothing is left to change while the window draws. A test
// parses each, so one that does not parse is a test's failure before it is an empty button.
var parsedGlyphs = func() map[string][]glyphStep {
	parsed := make(map[string][]glyphStep, len(glyphs))
	for name, path := range glyphs {
		parsed[name], _ = parseGlyph(path)
	}
	return parsed
}()

// parseGlyph reads the part of the SVG path language that the glyphs use.
func parseGlyph(path string) ([]glyphStep, error) {
	var steps []glyphStep
	for at := 0; at < len(path); {
		command := path[at]
		at++
		end := at
		for end < len(path) && !strings.ContainsRune("MLCZ", rune(path[end])) {
			end++
		}
		fields := strings.Fields(path[at:end])
		at = end
		want := map[byte]int{'M': 2, 'L': 2, 'C': 6, 'Z': 0}
		count, known := want[command]
		if !known || len(fields) != count {
			return nil, fmt.Errorf("%c with %d numbers", command, len(fields))
		}
		step := glyphStep{command: command}
		for i := 0; i < count; i += 2 {
			x, errX := strconv.ParseFloat(fields[i], 32)
			y, errY := strconv.ParseFloat(fields[i+1], 32)
			if errX != nil || errY != nil {
				return nil, fmt.Errorf("%q is not a point", fields[i]+" "+fields[i+1])
			}
			step.points = append(step.points, f32.Pt(float32(x), float32(y)))
		}
		steps = append(steps, step)
	}
	return steps, nil
}

func drawGlyph(gtx C, name string, size int, ink color.NRGBA) {
	steps := parsedGlyphs[name]
	scale := float32(size) / glyphGrid
	var path clip.Path
	path.Begin(gtx.Ops)
	place := func(point f32.Point) f32.Point { return f32.Pt(point.X*scale, point.Y*scale) }
	for _, step := range steps {
		switch step.command {
		case 'M':
			path.MoveTo(place(step.points[0]))
		case 'L':
			path.LineTo(place(step.points[0]))
		case 'C':
			path.CubeTo(place(step.points[0]), place(step.points[1]), place(step.points[2]))
		case 'Z':
			path.Close()
		}
	}
	paint.FillShape(gtx.Ops, ink, clip.Stroke{Path: path.End(), Width: glyphStroke * scale}.Op())
}

func buttonColors(kind buttonKind) (background, ink color.NRGBA) {
	switch kind {
	case buttonMain:
		return colorPrimary, colorStrong
	case buttonLosing:
		return colorQuiet, colorDanger
	default:
		return colorQuiet, colorText
	}
}

func lighter(shade color.NRGBA) color.NRGBA {
	up := func(value uint8) uint8 { return uint8(min(int(value)+24, 0xFF)) }
	return color.NRGBA{R: up(shade.R), G: up(shade.G), B: up(shade.B), A: shade.A}
}

// A button's hover of its own, as csiPass has, read on every frame the button is drawn, enabled or not.
// A Clickable drawn disabled reads no events, so its own hover goes stale: under a window it would go
// on saying the pointer is there, and its tip follow the pointer across the window.
type buttonHover struct {
	hover gesture.Hover
	drawn uint64 // The frame it was last drawn in.
}

// hovered says whether the pointer is over a button. The hover is read from the window's own events
// even for a page under a modal window, so that it sees the pointer leave while the window is open,
// and the button has no tip left over when it closes. Only what reads events offers a tip or lights.
func (a *App) hovered(gtx C, click *widget.Clickable) bool {
	state := a.hovers[click]
	if state == nil {
		state = &buttonHover{}
		a.hovers[click] = state
	}
	state.drawn = a.frames
	over := state.hover.Update(a.source)
	return over && gtx.Source.Enabled()
}

// watchHover lays the button's hover over what it drew, letting presses through to it.
func (a *App) watchHover(gtx C, click *widget.Clickable, size image.Point) {
	area := clip.Rect{Max: size}.Push(gtx.Ops)
	pass := pointer.PassOp{}.Push(gtx.Ops)
	a.hovers[click].hover.Add(gtx.Ops)
	pass.Pop()
	area.Pop()
}

// forgetHovers drops those of buttons not drawn this frame, such as those of pages made anew.
func (a *App) forgetHovers() {
	for click, state := range a.hovers {
		if state.drawn != a.frames {
			delete(a.hovers, click)
		}
	}
}

// iconButton is only a glyph, for what acts on the row of a table it stands in. The tip says what it does.
func (a *App) iconButton(gtx C, click *widget.Clickable, kind buttonKind, glyph, tip string, enabled bool) D {
	gtx.Constraints.Min = image.Point{}
	over := a.hovered(gtx, click)
	if !enabled {
		gtx = gtx.Disabled()
	}
	if over {
		a.tipOffered = tip
	}
	size := gtx.Dp(iconButtonSize)
	drawn := gtx.Dp(glyphDrawnSize)
	dims := click.Layout(gtx, func(gtx C) D {
		background, ink := buttonColors(kind)
		if !enabled {
			background.A, ink.A = disabledOpacity, disabledOpacity
		} else if over {
			background = lighter(background)
		}
		fill(gtx, image.Pt(size, size), gtx.Dp(buttonCorner), background)
		offset := (size - drawn) / 2
		moved := op.Offset(image.Pt(offset, offset)).Push(gtx.Ops)
		drawGlyph(gtx, glyph, drawn, ink)
		moved.Pop()
		return D{Size: image.Pt(size, size)}
	})
	a.watchHover(gtx, click, dims.Size)
	return dims
}

const (
	copyButtonSize = unit.Dp(22)
	copyGlyphSize  = unit.Dp(14)
	copyCorner     = unit.Dp(4)
)

// copyButton is csiPass's: a small glyph beside a value, lit only under the pointer, that puts the
// value on the clipboard. With no value it is not there.
func (a *App) copyButton(gtx C, click *widget.Clickable, text string) D {
	if text == "" {
		return D{}
	}
	for click.Clicked(gtx) {
		gtx.Execute(clipboard.WriteCmd{Type: "application/text", Data: io.NopCloser(strings.NewReader(text))})
		a.report("Copied to the clipboard.")
	}
	over := a.hovered(gtx, click)
	if over {
		a.tipOffered = "Copy to the clipboard"
	}
	size := gtx.Dp(copyButtonSize)
	dims := click.Layout(gtx, func(gtx C) D {
		ink := colorMuted
		if over {
			fill(gtx, image.Pt(size, size), gtx.Dp(copyCorner), colorQuiet)
			ink = colorText
		}
		glyph := gtx.Dp(copyGlyphSize)
		moved := op.Offset(image.Pt((size-glyph)/2, (size-glyph)/2)).Push(gtx.Ops)
		drawGlyph(gtx, "copy", glyph, ink)
		moved.Pop()
		return D{Size: image.Pt(size, size)}
	})
	a.watchHover(gtx, click, dims.Size)
	return dims
}

// labelButton shows a glyph and its words, or the words alone with no glyph. A tip is for what they
// leave out, so it is empty when they say it all.
func (a *App) labelButton(gtx C, click *widget.Clickable, kind buttonKind, glyph, words, tip string, enabled bool) D {
	gtx.Constraints.Min = image.Point{}
	over := a.hovered(gtx, click)
	if !enabled {
		gtx = gtx.Disabled()
	}
	if over && tip != "" {
		a.tipOffered = tip
	}
	height := gtx.Dp(iconButtonSize)
	pad := gtx.Dp(labelPad)
	dims := click.Layout(gtx, func(gtx C) D {
		background, ink := buttonColors(kind)
		if !enabled {
			background.A, ink.A = disabledOpacity, disabledOpacity
		} else if over {
			background = lighter(background)
		}
		left, drawn := pad, 0
		if glyph != "" {
			drawn = gtx.Dp(labelGlyphSize)
			left = gtx.Dp(labelGlyphPad) + drawn + gtx.Dp(labelGlyphGap)
		}
		free := gtx
		free.Constraints.Max.X = max(gtx.Constraints.Max.X-left-pad, 0)
		macro := op.Record(gtx.Ops)
		style := a.label(sizeSmall, ink, words)
		style.MaxLines = 1
		sized := style.Layout(free)
		text := macro.Stop()
		size := image.Pt(left+sized.Size.X+pad, height)
		fill(gtx, size, gtx.Dp(buttonCorner), background)
		if glyph != "" {
			moved := op.Offset(image.Pt(gtx.Dp(labelGlyphPad), (height-drawn)/2)).Push(gtx.Ops)
			drawGlyph(gtx, glyph, drawn, ink)
			moved.Pop()
		}
		moved := op.Offset(image.Pt(left, (height-sized.Size.Y)/2)).Push(gtx.Ops)
		text.Add(gtx.Ops)
		moved.Pop()
		return D{Size: size}
	})
	a.watchHover(gtx, click, dims.Size)
	return dims
}

// The window's top layer. It lets every pointer event on to what is under it.
func (a *App) trackPointer(gtx C) {
	for {
		seen, ok := gtx.Event(pointer.Filter{Target: a, Kinds: pointer.Move | pointer.Enter | pointer.Drag})
		if !ok {
			break
		}
		if moved, isPointer := seen.(pointer.Event); isPointer {
			a.pointerAt = moved.Position
		}
	}
	area := clip.Rect{Max: gtx.Constraints.Max}.Push(gtx.Ops)
	pass := pointer.PassOp{}.Push(gtx.Ops)
	event.Op(gtx.Ops, a)
	pass.Pop()
	area.Pop()
}

func (a *App) layoutTip(gtx C) {
	offered := a.tipOffered
	a.tipOffered = ""
	if offered != a.tipWords {
		a.tipWords = offered
		a.tipSince = gtx.Now
	}
	if offered == "" {
		return
	}
	if due := a.tipSince.Add(tipDelay); gtx.Now.Before(due) {
		gtx.Execute(op.InvalidateCmd{At: due})
		return
	}
	pad := gtx.Dp(tipPad)
	free := gtx
	free.Constraints.Min = image.Point{}
	macro := op.Record(gtx.Ops)
	words := a.label(sizeSmall, colorStrong, offered).Layout(free)
	drawn := macro.Stop()
	size := words.Size.Add(image.Pt(2*pad, 2*pad))
	// Below the pointer, and kept inside the window, which the footer's buttons would leave.
	from := gtx.Dp(tipFromPointer)
	x := min(int(a.pointerAt.X), gtx.Constraints.Max.X-size.X)
	y := int(a.pointerAt.Y) + from
	if y+size.Y > gtx.Constraints.Max.Y {
		y = int(a.pointerAt.Y) - from - size.Y
	}
	moved := op.Offset(image.Pt(max(x, 0), max(y, 0))).Push(gtx.Ops)
	fill(gtx, size, gtx.Dp(buttonCorner), colorBorder)
	inner := op.Offset(image.Pt(pad, pad)).Push(gtx.Ops)
	drawn.Add(gtx.Ops)
	inner.Pop()
	moved.Pop()
}
