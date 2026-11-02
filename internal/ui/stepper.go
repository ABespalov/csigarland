// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: A whole number with its arrows and the mouse wheel, as csiPass's stepper, kept to a file.
// Contributors: None

package ui

import (
	"image"
	"strconv"
	"strings"

	"gioui.org/io/event"
	"gioui.org/io/pointer"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"
)

const stepperWide = unit.Dp(64)

// numberField is a number the file holds. It shows the file's value, but for while it is being typed
// in, and what is typed goes to the file once it is a whole number in range.
type numberField struct {
	editor widget.Editor
	steps  pickerControl
}

// numberStepper draws a field and calls set with a new value, from a press, the wheel or typing.
func (a *App) numberStepper(gtx C, f *numberField, value, low, high int, set func(int)) D {
	f.editor.SingleLine = true
	f.editor.Filter = "-0123456789"
	shown := strconv.Itoa(value)
	typing := gtx.Focused(&f.editor)
	if !typing && f.editor.Text() != shown {
		f.editor.SetText(shown)
	}
	step := func(by int) {
		if next := min(max(value+by, low), high); next != value {
			set(next)
		}
	}
	for f.steps.up.Clicked(gtx) {
		step(1)
	}
	for f.steps.down.Clicked(gtx) {
		step(-1)
	}
	for {
		seen, ok := gtx.Event(pointer.Filter{Target: &f.steps, Kinds: pointer.Scroll,
			ScrollY: pointer.ScrollRange{Min: -pickerMaxWheel, Max: pickerMaxWheel}})
		if !ok {
			break
		}
		if wheel, isPointer := seen.(pointer.Event); isPointer && wheel.Scroll.Y != 0 {
			if wheel.Scroll.Y < 0 {
				step(1)
			} else {
				step(-1)
			}
		}
	}
	for {
		seen, ok := f.editor.Update(gtx)
		if !ok {
			break
		}
		if _, changed := seen.(widget.ChangeEvent); !changed {
			continue
		}
		if typed, err := strconv.Atoi(strings.TrimSpace(f.editor.Text())); err == nil && typed >= low && typed <= high && typed != value {
			set(typed)
		}
	}
	wide := gtx.Dp(stepperWide)
	arrows := gtx.Dp(pickerArrows)
	height := gtx.Dp(pickerHeight)
	line := max(gtx.Dp(segmentLine), 1)
	size := image.Pt(wide+line+arrows, height)
	shape := clip.UniformRRect(image.Rectangle{Max: size}, gtx.Dp(pickerCorner))
	ground := shape.Push(gtx.Ops)
	paint.Fill(gtx.Ops, colorBackground)
	event.Op(gtx.Ops, &f.steps)
	paint.FillShape(gtx.Ops, colorBorder, clip.Rect{Min: image.Pt(wide, 0), Max: image.Pt(wide+line, height)}.Op())
	half := height / 2
	paint.FillShape(gtx.Ops, colorBorder, clip.Rect{Min: image.Pt(wide+line, half), Max: image.Pt(size.X, half+line)}.Op())
	a.pickerArrow(gtx, &f.steps.up, "up", image.Pt(wide+line, 0), image.Pt(arrows, half))
	a.pickerArrow(gtx, &f.steps.down, "down", image.Pt(wide+line, half+line), image.Pt(arrows, height-half-line))
	ground.Pop()
	paint.FillShape(gtx.Ops, colorBorder, clip.Stroke{Path: shape.Path(gtx.Ops), Width: float32(line)}.Op())
	pad := gtx.Dp(pickerPad)
	inside := gtx
	inside.Constraints = layout.Exact(image.Pt(wide-2*pad, height))
	moved := op.Offset(image.Pt(pad, 0)).Push(gtx.Ops)
	layout.W.Layout(inside, func(gtx C) D {
		// As high as its line, to stand in the middle of the box, and as wide as the box, to be pressed.
		gtx.Constraints.Min = image.Pt(gtx.Constraints.Max.X, 0)
		style := material.Editor(a.theme, &f.editor, strconv.Itoa(low))
		style.TextSize = sizeBody
		style.Color = colorStrong
		style.HintColor = colorMuted
		return style.Layout(gtx)
	})
	moved.Pop()
	return D{Size: size}
}

// A stepper with its name before it.
func (a *App) namedStepper(name string, f *numberField, value, low, high int, set func(int)) layout.Widget {
	return func(gtx C) D {
		return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
			layout.Rigid(a.label(sizeSmall, colorMuted, name).Layout),
			layout.Rigid(layout.Spacer{Width: gap}.Layout),
			layout.Rigid(func(gtx C) D { return a.numberStepper(gtx, f, value, low, high, set) }),
		)
	}
}
