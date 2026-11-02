// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: A number on a turning wheel, as a phone's time picker has it, and a line of them.
// Contributors: None

package ui

import (
	"fmt"
	"image"
	"image/color"
	"math"

	"gioui.org/f32"
	"gioui.org/gesture"
	"gioui.org/io/event"
	"gioui.org/io/pointer"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
)

const (
	wheelRow   = unit.Dp(44)
	wheelRows  = 5 // The value in the middle and two on each side, fading out.
	wheelSlop  = unit.Dp(4)
	wheelDigit = unit.Dp(30) // Per digit of the widest value, with room around it.
)

// wheel is one number on a wheel, as Samsung's time picker turns them: the value in the middle,
// large, its neighbours above and below smaller and fading. It turns by the mouse wheel, by a drag
// that carries the numbers with the pointer, or by a press on a neighbour, which comes to the middle.
type wheel struct {
	value, low, high int
	cyclic           bool // Past the last comes the first again, as on a clock.
	digits           int
	drag             gesture.Drag
	start, last      float32
	offset           float32 // How far the numbers are carried off their places, while dragged.
	moved            bool
}

func (w *wheel) set(value, low, high int, cyclic bool, digits int) {
	w.low, w.high, w.cyclic, w.digits = low, high, cyclic, digits
	w.value = min(max(value, low), high)
	w.offset, w.moved = 0, false
}

// at is the value by places from this one, or false where a wheel that does not turn round has none.
func (w *wheel) at(by int) (int, bool) {
	count := w.high - w.low + 1
	next := w.value + by
	if w.cyclic {
		return w.low + ((next-w.low)%count+count)%count, true
	}
	return next, next >= w.low && next <= w.high
}

func (w *wheel) step(by int) {
	if next, ok := w.at(by); ok {
		w.value = next
	} else if by > 0 {
		w.value = w.high
	} else {
		w.value = w.low
	}
}

// update takes the frame's drags, presses and turns of the mouse wheel.
func (w *wheel) update(gtx C, row int) {
	slop := float32(gtx.Dp(wheelSlop))
	half := float32(row*wheelRows) / 2
	for {
		seen, ok := w.drag.Update(gtx.Metric, gtx.Source, gesture.Vertical)
		if !ok {
			break
		}
		switch seen.Kind {
		case pointer.Press:
			w.start, w.last, w.offset, w.moved = seen.Position.Y, seen.Position.Y, 0, false
		case pointer.Drag:
			w.offset += seen.Position.Y - w.last
			w.last = seen.Position.Y
			w.moved = w.moved || math.Abs(float64(seen.Position.Y-w.start)) > float64(slop)
			// Dragged down, the number above comes to the middle.
			for w.offset > float32(row)/2 {
				w.step(-1)
				w.offset -= float32(row)
			}
			for w.offset < -float32(row)/2 {
				w.step(1)
				w.offset += float32(row)
			}
		case pointer.Release:
			if !w.moved {
				w.step(int(math.Round(float64((seen.Position.Y - half) / float32(row)))))
			}
			w.offset = 0
		case pointer.Cancel:
			w.offset = 0
		}
	}
	for {
		seen, ok := gtx.Event(pointer.Filter{Target: w, Kinds: pointer.Scroll,
			ScrollY: pointer.ScrollRange{Min: -pickerMaxWheel, Max: pickerMaxWheel}})
		if !ok {
			break
		}
		// Turned towards the user, the numbers go up and the next comes to the middle.
		if turn, isPointer := seen.(pointer.Event); isPointer && turn.Scroll.Y != 0 {
			if turn.Scroll.Y > 0 {
				w.step(1)
			} else {
				w.step(-1)
			}
		}
	}
}

// wheelPart is a wheel in a line of them, or the words between two.
type wheelPart struct {
	wheel *wheel
	words string
}

// wheels draws a line of wheels and what stands between them, on a band that marks the middle row.
// settle, if not nil, runs once the frame's turns are taken and before anything is drawn, for wheels
// that hold each other to what they can say together, as a day to its month.
func (a *App) wheels(gtx C, settle func(), parts ...wheelPart) D {
	row := gtx.Dp(wheelRow)
	high := row * wheelRows
	for _, part := range parts {
		if part.wheel != nil {
			part.wheel.update(gtx, row)
		}
	}
	if settle != nil {
		settle()
	}
	children := make([]layout.FlexChild, 0, len(parts))
	for _, part := range parts {
		if part.wheel == nil {
			children = append(children, layout.Rigid(func(gtx C) D {
				gtx.Constraints = layout.Exact(image.Pt(a.segmentWidth(gtx, part.words)+gtx.Dp(gap), high))
				return layout.Center.Layout(gtx, a.wheelWords(part.words, sizeTitle, colorStrong))
			}))
			continue
		}
		children = append(children, layout.Rigid(func(gtx C) D { return a.wheelColumn(gtx, part.wheel, row) }))
	}
	macro := op.Record(gtx.Ops)
	dims := layout.Flex{}.Layout(gtx, children...)
	drawn := macro.Stop()
	band := image.Rect(0, (high-row)/2, dims.Size.X, (high+row)/2)
	paint.FillShape(gtx.Ops, colorSurface, clip.UniformRRect(band, gtx.Dp(corner)).Op(gtx.Ops))
	drawn.Add(gtx.Ops)
	return dims
}

func (a *App) wheelWords(words string, size unit.Sp, ink color.NRGBA) layout.Widget {
	style := a.label(size, ink, words)
	style.MaxLines = 1
	return style.Layout
}

// One wheel: its numbers at their places, carried by a drag, fading with their distance from the middle.
func (a *App) wheelColumn(gtx C, w *wheel, row int) D {
	high := row * wheelRows
	wide := gtx.Dp(wheelDigit) * max(w.digits, 2)
	size := image.Pt(wide, high)
	area := clip.Rect{Max: size}.Push(gtx.Ops)
	w.drag.Add(gtx.Ops)
	event.Op(gtx.Ops, w)
	pointer.CursorPointer.Add(gtx.Ops)
	for k := -wheelRows/2 - 1; k <= wheelRows/2+1; k++ {
		value, ok := w.at(k)
		if !ok {
			continue
		}
		middle := float32(k*row) + w.offset
		distance := math.Abs(float64(middle)) / float64(row)
		if distance > float64(wheelRows)/2 {
			continue
		}
		ink, textSize := colorStrong, sizeTitle
		if distance >= 0.5 {
			ink, textSize = colorMuted, sizeHeading
			ink.A = uint8(float64(ink.A) * max(0.25, 1-distance/3))
		}
		top := int(float32(high-row)/2 + middle)
		moved := op.Offset(image.Pt(0, top)).Push(gtx.Ops)
		place := gtx
		place.Constraints = layout.Exact(image.Pt(wide, row))
		layout.Center.Layout(place, a.wheelWords(fmt.Sprintf("%0*d", w.digits, value), textSize, ink))
		moved.Pop()
	}
	area.Pop()
	return D{Size: size}
}

// The middle of a wheel's row, for a test to press on.
func wheelRowMiddle(gtx C, by int) f32.Point {
	row := gtx.Dp(wheelRow)
	return f32.Pt(float32(gtx.Dp(wheelDigit)), float32(row*wheelRows)/2+float32(by*row))
}
