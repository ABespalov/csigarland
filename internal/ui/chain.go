// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The preview's LEDs as a chain from the garland's board: numbered, with arrows, back and forth.
// Contributors: None

package ui

import (
	"fmt"
	"image"
	"image/color"
	"math"
	"strconv"
	"time"

	"gioui.org/f32"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/engine"
)

const (
	chainLedSize   = unit.Dp(24) // The LED itself.
	chainRayRoom   = unit.Dp(9)  // Around it, for its rays at full brightness.
	chainLinkGap   = unit.Dp(14) // Between two cells, under the arrow from one to the next.
	chainArrowHead = unit.Dp(4)
	chainNumberGap = unit.Dp(4)  // From the bottom of the LED to the top of its number.
	chainNumberRow = unit.Dp(14) // The number's own height, under the LED.
	chainNumberPad = unit.Dp(3)  // Either side of it, on the panel's colour over an arrow.
	chainArrowPad  = 3           // Dp between an arrow's ends and the LEDs it joins.
	chainRays      = 8           // Turned half a step, so that none lies along an arrow.
	rayWidthLeast  = 0.8         // Dp, for the dimmest LED that is on at all.
	rayWidthMost   = 2.6
	lineWidth      = 1.5
)

// chainColumns is how many cells a line holds in this many pixels, the board's among them. Never fewer
// than two, so that a line still has somewhere to go.
func chainColumns(width, cell, gap int) int {
	return max((width+gap)/(cell+gap), 2)
}

// chainPlace is where a cell of the chain stands. The board is cell 0 and LED n is cell n. Lines go left
// to right, then right to left, as a garland is hung back and forth.
func chainPlace(cell, columns int) (line, column int) {
	line, column = cell/columns, cell%columns
	if line%2 == 1 {
		column = columns - 1 - column
	}
	return line, column
}

// ledLight is how bright an LED is, 0 to 1: its brightest channel.
func ledLight(led engine.Color) float32 {
	return float32(max(led.R, led.G, led.B)) / 0xFF
}

// rayInk is the LED's hue at full strength. How bright it is, its rays say by their length and width.
func rayInk(led engine.Color) color.NRGBA {
	top := int(max(led.R, led.G, led.B))
	if top == 0 {
		return color.NRGBA{}
	}
	up := func(value uint8) uint8 { return uint8(int(value) * 0xFF / top) }
	return color.NRGBA{R: up(led.R), G: up(led.G), B: up(led.B), A: 0xFF}
}

// A dark LED is a dark circle, as a figurine still is with no light in it.
func ledColor(led engine.Color) color.NRGBA {
	const floor = 26
	return color.NRGBA{R: max(led.R, floor), G: max(led.G, floor), B: max(led.B, floor+8), A: 0xFF}
}

// previewLook is how the preview here plays: at a speed and a brightness of its own, which change
// nothing on the garland.
type previewLook struct {
	speed      widget.Float
	brightness widget.Float
	clock      previewClock
}

// seed starts the look at the garland's speed, or 1x with none to ask, and always at full brightness:
// a screen shows a garland turned down low as all but dark, where its LEDs in a room are not.
func (l *previewLook) seed(state api.State) {
	speed := state.Speed
	if speed <= 0 {
		speed = 1
	}
	l.speed.Value = SpeedToSlider(speed)
	l.brightness.Value = PercentToSlider(100)
}

// takeFrom has the look of another window, for the effect to go on looking the same.
func (l *previewLook) takeFrom(other *previewLook) {
	l.speed.Value, l.brightness.Value = other.speed.Value, other.brightness.Value
}

func (l *previewLook) pace() float64 { return SliderToSpeed(l.speed.Value) }
func (l *previewLook) percent() int  { return SliderToPercent(l.brightness.Value) }

// previewClock is the effect's own time, which runs at the speed chosen, as the Lighting task's does:
// a change of speed changes how fast it goes on from where it is, not where it is.
type previewClock struct {
	played time.Duration
	last   time.Time
}

func (c *previewClock) restart() { *c = previewClock{} }

func (c *previewClock) advance(now time.Time, speed float64) uint32 {
	if !c.last.IsZero() && now.After(c.last) {
		c.played += time.Duration(float64(now.Sub(c.last)) * speed)
	}
	c.last = now
	return uint32(c.played / time.Millisecond)
}

// lookSliders are the preview's speed and brightness, under its LEDs.
func (a *App) lookSliders(look *previewLook) layout.Widget {
	return a.sliderPairs(
		a.slider("Speed", &look.speed, SpeedText(look.pace())),
		a.slider("Brightness", &look.brightness, fmt.Sprintf("%d%%", look.percent())),
	)
}

// sliderPairs lays sliders two to a line, as one across a whole window is longer than a hand moves,
// and one left over across the whole line.
func (a *App) sliderPairs(sliders ...layout.Widget) layout.Widget {
	return func(gtx C) D {
		var lines []layout.Widget
		for i := 0; i < len(sliders); i += 2 {
			if i+1 == len(sliders) {
				lines = append(lines, sliders[i])
				break
			}
			left, right := sliders[i], sliders[i+1]
			lines = append(lines, func(gtx C) D {
				return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
					layout.Flexed(1, left),
					layout.Rigid(layout.Spacer{Width: gapLarge * 2}.Layout),
					layout.Flexed(1, right),
				)
			})
		}
		return a.column(gtx, lines...)
	}
}

// layoutStrip draws the effect at this moment, as the firmware's engine has it, on a chain as wide as
// there is room for.
func (a *App) layoutStrip(gtx C, preview *engine.Preview, look *previewLook, frame []engine.Color) D {
	if preview != nil {
		preview.Frame(look.clock.advance(gtx.Now, look.pace()), look.percent(), frame)
		gtx.Execute(op.InvalidateCmd{At: gtx.Now.Add(previewFrame)})
	} else {
		clear(frame)
	}
	led := gtx.Dp(chainLedSize)
	room := gtx.Dp(chainRayRoom)
	cell := led + 2*room
	gap := gtx.Dp(chainLinkGap)
	// A number under each LED, in the room its rays would have below it.
	numberTop := room + led + gtx.Dp(chainNumberGap)
	line := max(cell, numberTop+gtx.Dp(chainNumberRow))
	columns := chainColumns(gtx.Constraints.Max.X, cell, gap)
	corner := func(index int) image.Point {
		row, column := chainPlace(index, columns)
		return image.Pt(column*(cell+gap), row*(line+gap/2))
	}
	cells := len(frame) + 1
	// The arrows, then the numbers over the arrows that turn down, then the LEDs with their rays.
	for i := 1; i < cells; i++ {
		drawLink(gtx, corner(i-1), corner(i), cell, led)
	}
	// Numbered from 0, as an effect file's start and stop count them.
	for i := 1; i < cells; i++ {
		a.drawNumber(gtx, corner(i).Add(image.Pt(0, numberTop)), cell, i-1)
	}
	drawBoard(gtx, corner(0), cell, led)
	for i, shade := range frame {
		drawLed(gtx, corner(i+1), cell, led, shade)
	}
	lines := (cells + columns - 1) / columns
	return D{Size: image.Pt(min(cells, columns)*(cell+gap)-gap, lines*(line+gap/2)-gap/2)}
}

func cellCenter(at image.Point, cell int) f32.Point {
	return f32.Pt(float32(at.X)+float32(cell)/2, float32(at.Y)+float32(cell)/2)
}

// An arrow from one cell to the next, from the edge of one to the edge of the other.
func drawLink(gtx C, from, to image.Point, cell, led int) {
	start, end := cellCenter(from, cell), cellCenter(to, cell)
	dx, dy := end.X-start.X, end.Y-start.Y
	length := float32(math.Hypot(float64(dx), float64(dy)))
	if length == 0 {
		return
	}
	px := gtx.Metric.PxPerDp
	ux, uy := dx/length, dy/length
	skip := float32(led)/2 + chainArrowPad*px
	start = f32.Pt(start.X+ux*skip, start.Y+uy*skip)
	end = f32.Pt(end.X-ux*skip, end.Y-uy*skip)
	head := float32(gtx.Dp(chainArrowHead))
	var path clip.Path
	path.Begin(gtx.Ops)
	path.MoveTo(start)
	path.LineTo(end)
	path.MoveTo(f32.Pt(end.X-ux*head-uy*head, end.Y-uy*head+ux*head))
	path.LineTo(end)
	path.LineTo(f32.Pt(end.X-ux*head+uy*head, end.Y-uy*head-ux*head))
	paint.FillShape(gtx.Ops, colorBorder, clip.Stroke{Path: path.End(), Width: lineWidth * px}.Op())
}

// The garland's board, where the chain starts.
func drawBoard(gtx C, at image.Point, cell, led int) {
	size := led + gtx.Dp(chainRayRoom)
	offset := (cell - size) / 2
	moved := op.Offset(at.Add(image.Pt(offset, offset))).Push(gtx.Ops)
	fill(gtx, image.Pt(size, size), gtx.Dp(buttonCorner), colorQuiet)
	glyph := gtx.Dp(glyphDrawnSize)
	inner := op.Offset(image.Pt((size-glyph)/2, (size-glyph)/2)).Push(gtx.Ops)
	drawGlyph(gtx, "board", glyph, colorText)
	inner.Pop()
	moved.Pop()
}

// One LED: dark with no light, and with rays of its colour as long and as wide as it is bright.
func drawLed(gtx C, at image.Point, cell, led int, shade engine.Color) {
	if light := ledLight(shade); light > 0 {
		drawRays(gtx, cellCenter(at, cell), float32(led)/2, light, rayInk(shade))
	}
	offset := (cell - led) / 2
	circle := clip.Ellipse{Min: at.Add(image.Pt(offset, offset)), Max: at.Add(image.Pt(offset+led, offset+led))}
	paint.FillShape(gtx.Ops, ledColor(shade), circle.Op(gtx.Ops))
}

// An LED's number, centred under it from top, on the panel's colour so that an arrow under it
// does not cross it.
func (a *App) drawNumber(gtx C, top image.Point, cell, number int) {
	free := gtx
	free.Constraints = layout.Constraints{Max: image.Pt(cell, cell)}
	macro := op.Record(gtx.Ops)
	words := a.label(sizeCaption, colorMuted, strconv.Itoa(number)).Layout(free)
	drawn := macro.Stop()
	pad := gtx.Dp(chainNumberPad)
	size := words.Size.Add(image.Pt(2*pad, 0))
	moved := op.Offset(top.Add(image.Pt((cell-size.X)/2, 0))).Push(gtx.Ops)
	fill(gtx, size, size.Y/2, colorPanel)
	inner := op.Offset(image.Pt(pad, 0)).Push(gtx.Ops)
	drawn.Add(gtx.Ops)
	inner.Pop()
	moved.Pop()
}

func drawRays(gtx C, center f32.Point, radius, light float32, ink color.NRGBA) {
	px := gtx.Metric.PxPerDp
	start := radius + 2*px
	// To a pixel short of the cell's edge at full brightness.
	length := (float32(gtx.Dp(chainRayRoom)) - 3*px) * light
	if length < 1 {
		return
	}
	var path clip.Path
	path.Begin(gtx.Ops)
	for i := range chainRays {
		angle := 2 * math.Pi * (float64(i) + 0.5) / chainRays
		ux, uy := float32(math.Cos(angle)), float32(math.Sin(angle))
		path.MoveTo(f32.Pt(center.X+ux*start, center.Y+uy*start))
		path.LineTo(f32.Pt(center.X+ux*(start+length), center.Y+uy*(start+length)))
	}
	width := px * (rayWidthLeast + (rayWidthMost-rayWidthLeast)*light)
	paint.FillShape(gtx.Ops, ink, clip.Stroke{Path: path.End(), Width: width}.Op())
}
