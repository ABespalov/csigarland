// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The utility's colours, type and small drawing helpers, after the portal's.
// Contributors: None

// Package ui is the utility's window, in Gio. It repeats what the portal does, and adds tuning
// effects with a preview drawn by the firmware's own engine, and reading the garland's log.
package ui

import (
	"image"
	"image/color"
	"strings"
	"time"

	"gioui.org/font"
	"gioui.org/font/opentype"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/text"
	"gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"
	"github.com/rs/zerolog/log"
	"golang.org/x/image/font/gofont/gobold"
	"golang.org/x/image/font/gofont/gomono"
	"golang.org/x/image/font/gofont/goregular"
)

type (
	C = layout.Context
	D = layout.Dimensions
)

// As the portal has them.
var (
	colorBackground = color.NRGBA{R: 0x0F, G: 0x17, B: 0x2A, A: 0xFF}
	colorPanel      = color.NRGBA{R: 0x1E, G: 0x29, B: 0x3B, A: 0xFF}
	colorSurface    = color.NRGBA{R: 0x15, G: 0x20, B: 0x33, A: 0xFF}
	colorQuiet      = color.NRGBA{R: 0x33, G: 0x41, B: 0x55, A: 0xFF}
	colorBorder     = color.NRGBA{R: 0x47, G: 0x55, B: 0x69, A: 0xFF}
	colorText       = color.NRGBA{R: 0xE2, G: 0xE8, B: 0xF0, A: 0xFF}
	colorStrong     = color.NRGBA{R: 0xF8, G: 0xFA, B: 0xFC, A: 0xFF}
	colorMuted      = color.NRGBA{R: 0x94, G: 0xA3, B: 0xB8, A: 0xFF}
	colorPrimary    = color.NRGBA{R: 0x25, G: 0x63, B: 0xEB, A: 0xFF}
	colorBrand      = color.NRGBA{R: 0x3B, G: 0x82, B: 0xF6, A: 0xFF}
	colorActive     = color.NRGBA{R: 0xFB, G: 0xBF, B: 0x24, A: 0xFF}
	colorDanger     = color.NRGBA{R: 0xFC, G: 0xA5, B: 0xA5, A: 0xFF}
	colorGood       = color.NRGBA{R: 0x86, G: 0xEF, B: 0xAC, A: 0xFF}
)

const (
	sizeBody    = unit.Sp(15)
	sizeSmall   = unit.Sp(13)
	sizeCaption = unit.Sp(11)
	sizeTagline = unit.Sp(10)
	sizeHeading = unit.Sp(17)
	sizeTitle   = unit.Sp(24)

	gapSmall = unit.Dp(4)
	gap      = unit.Dp(8)
	gapLarge = unit.Dp(14)
	gapPage  = unit.Dp(16)
	corner   = unit.Dp(8)
)

// The type travels with the program, so that it looks the same on any machine: see csiPass.
func newTheme() *material.Theme {
	var faces []font.FontFace
	for _, data := range [][]byte{goregular.TTF, gobold.TTF, gomono.TTF} {
		parsed, err := opentype.ParseCollection(data)
		if err != nil {
			log.Error().Err(err).Msg("a bundled typeface would not parse")
			continue
		}
		faces = append(faces, parsed[0])
	}
	theme := material.NewTheme()
	theme.Shaper = text.NewShaper(text.WithCollection(faces))
	theme.Palette.Bg = colorBackground
	theme.Palette.Fg = colorText
	theme.Palette.ContrastBg = colorPrimary
	theme.Palette.ContrastFg = colorStrong
	theme.TextSize = sizeBody
	return theme
}

func fill(gtx C, size image.Point, radius int, background color.NRGBA) {
	defer clip.UniformRRect(image.Rectangle{Max: size}, radius).Push(gtx.Ops).Pop()
	paint.Fill(gtx.Ops, background)
}

func (a *App) label(size unit.Sp, shade color.NRGBA, words string) material.LabelStyle {
	style := material.Label(a.theme, size, words)
	style.Color = shade
	return style
}

func (a *App) body(words string) layout.Widget { return a.label(sizeBody, colorText, words).Layout }
func (a *App) hint(words string) layout.Widget { return a.label(sizeSmall, colorMuted, words).Layout }

func (a *App) heading(words string) layout.Widget {
	style := a.label(sizeHeading, colorStrong, words)
	style.Font.Weight = font.Bold
	return style.Layout
}

// Status is a line of what happened, red if it went wrong.
func (a *App) statusLine(words string, failed bool) layout.Widget {
	return a.label(sizeSmall, statusInk(failed), words).Layout
}

func statusInk(failed bool) color.NRGBA {
	if failed {
		return colorDanger
	}
	return colorGood
}

// The rail's captions, as csiPass has them.
func upperWords(words string) string { return strings.ToUpper(words) }

type buttonKind int

const (
	buttonMain   buttonKind = iota // Blue: the one main action of its block.
	buttonOther                    // Grey: any other.
	buttonLosing                   // Grey with red words: it loses something.
)

// A button whose words are its value, such as a speed or a network's name, with a tip for what it does.
func (a *App) button(gtx C, click *widget.Clickable, kind buttonKind, words, tip string, enabled bool) D {
	return a.labelButton(gtx, click, kind, "", words, tip, enabled)
}

// How long a second press of a button that loses something counts as meaning it.
const confirmWindow = 4 * time.Second

// confirmClick is a button that loses something: the first press only asks.
type confirmClick struct {
	click      widget.Clickable
	armedUntil time.Time
}

func (c *confirmClick) press(now time.Time) bool {
	if now.Before(c.armedUntil) {
		c.armedUntil = time.Time{}
		return true
	}
	c.armedUntil = now.Add(confirmWindow)
	return false
}

func (c *confirmClick) confirmed(gtx C) bool {
	return c.click.Clicked(gtx) && c.press(gtx.Now)
}

func (a *App) confirmButton(gtx C, c *confirmClick, kind buttonKind, glyph, tip string, enabled bool) D {
	if gtx.Now.Before(c.armedUntil) {
		kind, glyph, tip = buttonLosing, "check", "Press again: "+tip
		gtx.Execute(op.InvalidateCmd{At: c.armedUntil})
	}
	return a.iconButton(gtx, &c.click, kind, glyph, tip, enabled)
}

// As confirmButton, with its words.
func (a *App) confirmLabelButton(gtx C, c *confirmClick, kind buttonKind, glyph, words, tip string, enabled bool) D {
	if gtx.Now.Before(c.armedUntil) {
		if tip == "" {
			tip = words
		}
		kind, glyph, words, tip = buttonLosing, "check", "Press again", "Press again: "+tip
		gtx.Execute(op.InvalidateCmd{At: c.armedUntil})
	}
	return a.labelButton(gtx, &c.click, kind, glyph, words, tip, enabled)
}

// A block of the page, as the portal's sections are.
func (a *App) card(gtx C, title string, rows ...layout.Widget) D {
	macro := layout.UniformInset(gapLarge)
	return layout.Stack{}.Layout(gtx,
		layout.Expanded(func(gtx C) D {
			fill(gtx, gtx.Constraints.Min, gtx.Dp(corner), colorPanel)
			return D{Size: gtx.Constraints.Min}
		}),
		layout.Stacked(func(gtx C) D {
			gtx.Constraints.Min.X = gtx.Constraints.Max.X
			return macro.Layout(gtx, func(gtx C) D {
				all := make([]layout.Widget, 0, len(rows)+1)
				if title != "" {
					all = append(all, a.heading(title))
				}
				all = append(all, rows...)
				return a.column(gtx, all...)
			})
		}),
	)
}

// Rows with a gap between, each as wide as there is room for.
func (a *App) column(gtx C, rows ...layout.Widget) D {
	children := make([]layout.FlexChild, 0, len(rows)*2)
	for i, row := range rows {
		if i > 0 {
			children = append(children, layout.Rigid(layout.Spacer{Height: gap}.Layout))
		}
		children = append(children, layout.Rigid(row))
	}
	return layout.Flex{Axis: layout.Vertical, Alignment: layout.Start}.Layout(gtx, children...)
}

// Things side by side with a gap between, going on to a new line when one no longer fits.
func row(gtx C, items ...layout.Widget) D {
	type drawn struct {
		call op.CallOp
		size image.Point
		line int
	}
	space := gtx.Dp(gap)
	free := gtx
	free.Constraints.Min = image.Point{}
	placed := make([]drawn, len(items))
	heights := []int{0}
	x := 0
	for i, item := range items {
		macro := op.Record(gtx.Ops)
		dims := item(free)
		placed[i] = drawn{call: macro.Stop(), size: dims.Size}
		if x > 0 && x+space+dims.Size.X > gtx.Constraints.Max.X {
			heights = append(heights, 0)
			x = 0
		}
		if x > 0 {
			x += space
		}
		x += dims.Size.X
		line := len(heights) - 1
		placed[i].line = line
		heights[line] = max(heights[line], dims.Size.Y)
	}
	var total image.Point
	x = 0
	line := 0
	y := 0
	for _, item := range placed {
		if item.line != line {
			y += heights[line] + space
			line = item.line
			x = 0
		}
		if x > 0 {
			x += space
		}
		moved := op.Offset(image.Pt(x, y+(heights[line]-item.size.Y)/2)).Push(gtx.Ops)
		item.call.Add(gtx.Ops)
		moved.Pop()
		x += item.size.X
		total.X = max(total.X, x)
	}
	total.Y = y + heights[line]
	return D{Size: gtx.Constraints.Constrain(total)}
}

// A named value, as the portal's lists of facts are.
func (a *App) fact(name, value string) layout.Widget {
	return func(gtx C) D {
		return layout.Flex{}.Layout(gtx,
			layout.Rigid(func(gtx C) D {
				gtx.Constraints.Min.X = gtx.Dp(unit.Dp(130))
				gtx.Constraints.Max.X = gtx.Constraints.Min.X
				return a.label(sizeSmall, colorMuted, name).Layout(gtx)
			}),
			layout.Flexed(1, a.label(sizeSmall, colorText, value).Layout),
		)
	}
}

// copyFact is a named value with a button beside it that puts the value on the clipboard, as csiPass has
// for what is copied rather than typed: a hash, a MAC, a serial.
func (a *App) copyFact(name, value string, click *widget.Clickable) layout.Widget {
	return func(gtx C) D {
		return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
			layout.Rigid(func(gtx C) D {
				gtx.Constraints.Min.X = gtx.Dp(unit.Dp(130))
				gtx.Constraints.Max.X = gtx.Constraints.Min.X
				return a.label(sizeSmall, colorMuted, name).Layout(gtx)
			}),
			layout.Rigid(a.label(sizeSmall, colorText, value).Layout),
			layout.Rigid(layout.Spacer{Width: gap}.Layout),
			layout.Rigid(func(gtx C) D { return a.copyButton(gtx, click, value) }),
		)
	}
}

// A field: a line of words with a border.
func (a *App) field(gtx C, editor *widget.Editor, hint string, width unit.Dp) D {
	if width > 0 {
		gtx.Constraints.Max.X = min(gtx.Constraints.Max.X, gtx.Dp(width))
		gtx.Constraints.Min.X = gtx.Constraints.Max.X
	} else {
		gtx.Constraints.Min.X = gtx.Constraints.Max.X
	}
	border := widget.Border{Color: colorBorder, Width: unit.Dp(1), CornerRadius: unit.Dp(6)}
	return border.Layout(gtx, func(gtx C) D {
		return layout.UniformInset(unit.Dp(7)).Layout(gtx, func(gtx C) D {
			style := material.Editor(a.theme, editor, hint)
			style.TextSize = sizeBody
			style.Color = colorStrong
			style.HintColor = colorMuted
			return style.Layout(gtx)
		})
	})
}

func newField(single bool) widget.Editor {
	return widget.Editor{SingleLine: single, Submit: single}
}

func (a *App) checkbox(gtx C, value *widget.Bool, words string) D {
	style := material.CheckBox(a.theme, value, words)
	style.Size = unit.Dp(20)
	style.TextSize = sizeBody
	style.Color = colorText
	style.IconColor = colorBrand
	return style.Layout(gtx)
}
