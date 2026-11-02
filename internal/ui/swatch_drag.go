// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: A line of a field's colours, each pressed to pick it and dragged to go before or after another.
// Contributors: None

package ui

import (
	"fmt"
	"image"
	"slices"

	"gioui.org/f32"
	"gioui.org/gesture"
	"gioui.org/io/pointer"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget"
)

const (
	dragSlop   = unit.Dp(4)
	insertMark = unit.Dp(3)
)

// swatchSlot is one colour's swatch: a press picks it, a drag carries it.
type swatchSlot struct {
	click widget.Clickable
	drag  gesture.Drag
}

// colourLine is where a field's colours stand, and the one carried, if any. A drag reports where the
// pointer is in its swatch, so the line keeps where each swatch was drawn to know where the pointer
// is among them all.
type colourLine struct {
	slots    [maxPaletteStops]swatchSlot
	carrying bool
	moved    bool // Past the slop: a drag, not a press that shook.
	from     int
	grabbed  f32.Point // Where the swatch was taken, in it.
	pointer  f32.Point // Where the pointer is, in the line.
	items    []image.Rectangle
	swatches []image.Point
}

// moveItem takes the item at from out and puts it at to, the others closing up behind it.
func moveItem[T any](items []T, from, to int) []T {
	if from == to || from < 0 || to < 0 || from >= len(items) || to >= len(items) {
		return items
	}
	item := items[from]
	return slices.Insert(slices.Delete(slices.Clone(items), from, from+1), to, item)
}

// insertionAt is the gap the pointer is nearest, 0 before the first item to len(items) after the
// last: on the line of items it is over, before the first whose middle is right of it.
func insertionAt(items []image.Rectangle, at image.Point) int {
	if len(items) == 0 {
		return 0
	}
	// The line: the one it is in, or the nearest.
	best, distance := 0, -1
	for i, item := range items {
		d := 0
		switch {
		case at.Y < item.Min.Y:
			d = item.Min.Y - at.Y
		case at.Y >= item.Max.Y:
			d = at.Y - item.Max.Y + 1
		}
		if distance < 0 || d < distance {
			best, distance = i, d
		}
	}
	top := items[best].Min.Y
	last := -1
	for i, item := range items {
		if item.Min.Y != top {
			continue
		}
		if at.X < (item.Min.X+item.Max.X)/2 {
			return i
		}
		last = i
	}
	return last + 1
}

// landing is where a colour taken from from goes when dropped in the gap gap.
func landing(from, gap int) int {
	if gap > from {
		return gap - 1
	}
	return gap
}

// colourLine lays out count colours, each "Colour N", its swatch, and after(i) if not nil, then the
// tail, on as many lines as the width needs. A press on a swatch calls picked, and a colour dropped
// before or after another calls moved with where it was and where it now is.
func (a *App) colourLine(gtx C, l *colourLine, count int, colour func(int) RGB, after func(int) layout.Widget,
	tip string, tail []layout.Widget, picked func(int), moved func(from, to int)) D {
	count = min(count, len(l.slots))
	slop := float32(gtx.Dp(dragSlop))
	for i := range count {
		slot := &l.slots[i]
		for {
			seen, ok := slot.drag.Update(gtx.Metric, gtx.Source, gesture.Both)
			if !ok {
				break
			}
			if i >= len(l.swatches) {
				continue
			}
			at := f32.Pt(float32(l.swatches[i].X), float32(l.swatches[i].Y)).Add(seen.Position)
			switch seen.Kind {
			case pointer.Press:
				l.carrying, l.moved, l.from, l.grabbed, l.pointer = true, false, i, seen.Position, at
			case pointer.Drag:
				if l.carrying && l.from == i {
					l.pointer = at
					shift := seen.Position.Sub(l.grabbed)
					l.moved = l.moved || shift.X*shift.X+shift.Y*shift.Y > slop*slop
				}
			case pointer.Release:
				if l.carrying && l.moved && l.from == i {
					l.pointer = at
					if to := landing(i, insertionAt(l.items, l.pointer.Round())); to != i {
						moved(i, to)
					}
				}
				l.carrying = false
			case pointer.Cancel:
				l.carrying = false
			}
		}
	}
	if l.carrying && l.from >= count {
		l.carrying = false
	}
	for i := range count {
		if l.slots[i].click.Clicked(gtx) {
			picked(i)
		}
	}
	return a.layoutColourLine(gtx, l, count, colour, after, tip, tail)
}

type placed struct {
	call op.CallOp
	at   image.Point
	size image.Point
}

func record(gtx C, w layout.Widget) placed {
	macro := op.Record(gtx.Ops)
	dims := w(gtx)
	return placed{call: macro.Stop(), size: dims.Size}
}

func (a *App) layoutColourLine(gtx C, l *colourLine, count int, colour func(int) RGB, after func(int) layout.Widget,
	tip string, tail []layout.Widget) D {
	free := gtx
	free.Constraints.Min = image.Point{}
	space, pad := gtx.Dp(gap), gtx.Dp(colorItemGap)
	size := gtx.Dp(swatchSize)
	carried := l.carrying && l.moved
	type item struct {
		parts  []placed
		swatch image.Point // In the item.
		size   image.Point
	}
	items := make([]item, 0, count+len(tail))
	for i := range count {
		words := record(free, a.label(sizeSmall, colorMuted, fmt.Sprintf("Colour %d", i+1)).Layout)
		x := words.size.X + space
		parts := []placed{words}
		height := max(words.size.Y, size)
		var next placed
		if after != nil {
			if w := after(i); w != nil {
				next = record(free, w)
				height = max(height, next.size.Y)
			}
		}
		swatchAt := image.Pt(x, (height-size)/2)
		width := x + size
		parts[0].at = image.Pt(0, (height-words.size.Y)/2)
		if next.size.X > 0 {
			next.at = image.Pt(width+space, (height-next.size.Y)/2)
			width = next.at.X + next.size.X
			parts = append(parts, next)
		}
		items = append(items, item{parts: parts, swatch: swatchAt, size: image.Pt(width, height)})
	}
	for _, w := range tail {
		drawn := record(free, w)
		items = append(items, item{parts: []placed{drawn}, size: drawn.size})
	}
	// Placed as row places them, with room after each colour.
	at := make([]image.Point, len(items))
	x, y, line, width := 0, 0, 0, 0
	for i, it := range items {
		if x > 0 && x+it.size.X > gtx.Constraints.Max.X {
			x, y, line = 0, y+line+space, 0
		}
		at[i] = image.Pt(x, y)
		x += it.size.X + space
		if i < count {
			x += pad
		}
		line = max(line, it.size.Y)
		width = max(width, at[i].X+it.size.X)
	}
	// Each item of a line is as high as the line, to take the gap the pointer is in on the line.
	l.items, l.swatches = l.items[:0], l.swatches[:0]
	for i := range count {
		high := items[i].size.Y
		for j := range items {
			if at[j].Y == at[i].Y {
				high = max(high, items[j].size.Y)
			}
		}
		l.items = append(l.items, image.Rectangle{Min: at[i], Max: at[i].Add(image.Pt(items[i].size.X, high))})
		l.swatches = append(l.swatches, at[i].Add(items[i].swatch))
	}
	for i, it := range items {
		moved := op.Offset(at[i]).Push(gtx.Ops)
		for _, part := range it.parts {
			offset := op.Offset(part.at).Push(gtx.Ops)
			part.call.Add(gtx.Ops)
			offset.Pop()
		}
		if i < count {
			a.drawSlot(gtx, l, i, it.swatch, colour(i), tip, count > 1, carried && l.from == i)
		}
		moved.Pop()
	}
	if carried {
		a.drawCarried(gtx, l, count, space, pad, colour(l.from))
	}
	return D{Size: image.Pt(width, y+line)}
}

// The swatch, and over it the place a drag starts, which passes the pointer on to the press.
func (a *App) drawSlot(gtx C, l *colourLine, i int, at image.Point, c RGB, tip string, draggable, carried bool) {
	slot := &l.slots[i]
	size := gtx.Dp(swatchSize)
	offset := op.Offset(at).Push(gtx.Ops)
	defer offset.Pop()
	area := image.Rectangle{Max: image.Pt(size, size)}
	if carried {
		// Its place stays while it is carried, empty.
		shape := clip.UniformRRect(area, gtx.Dp(buttonCorner))
		paint.FillShape(gtx.Ops, colorBorder, clip.Stroke{Path: shape.Path(gtx.Ops), Width: float32(max(gtx.Dp(segmentLine), 1))}.Op())
	} else {
		a.swatchButton(gtx, &slot.click, c, false, tip)
	}
	if draggable {
		pass := pointer.PassOp{}.Push(gtx.Ops)
		zone := clip.Rect(area).Push(gtx.Ops)
		slot.drag.Add(gtx.Ops)
		pointer.CursorGrab.Add(gtx.Ops)
		zone.Pop()
		pass.Pop()
	}
}

// What a drag shows: a mark in the gap the colour would go to, and the colour under the pointer.
func (a *App) drawCarried(gtx C, l *colourLine, count, space, pad int, c RGB) {
	gap := insertionAt(l.items, l.pointer.Round())
	if landing(l.from, gap) != l.from && count > 0 {
		mark := max(gtx.Dp(insertMark), 1)
		var x int
		var item image.Rectangle
		switch {
		case gap == count:
			item = l.items[count-1]
			x = item.Max.X + pad/2
		case gap > 0 && l.items[gap-1].Min.Y == l.items[gap].Min.Y:
			item = l.items[gap]
			x = (l.items[gap-1].Max.X + item.Min.X) / 2
		default:
			item = l.items[gap]
			x = item.Min.X - space/2
		}
		x = max(x-mark/2, 0)
		paint.FillShape(gtx.Ops, colorPrimary, clip.UniformRRect(image.Rect(x, item.Min.Y, x+mark, item.Max.Y), mark/2).Op(gtx.Ops))
	}
	macro := op.Record(gtx.Ops)
	under := op.Offset(l.pointer.Sub(l.grabbed).Round()).Push(gtx.Ops)
	swatch(gtx, c, true)
	under.Pop()
	op.Defer(gtx.Ops, macro.Stop())
}
