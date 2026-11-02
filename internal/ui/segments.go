// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The segmented control csiPass has for a choice among a few options, in place of radio buttons.
// Contributors: None

package ui

import (
	"image"
	"image/color"

	"gioui.org/font"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/clip"
	"gioui.org/op/paint"
	"gioui.org/unit"
	"gioui.org/widget"
)

const (
	segmentHeight = unit.Dp(32)
	segmentPad    = unit.Dp(12)
	segmentCorner = unit.Dp(6)
	segmentLine   = unit.Dp(1)
)

// segment is one option: what the choice holds when it is chosen, and what it says.
type segment struct{ key, words string }

// choice is one of a few options, drawn as csiPass draws its choosers: one rounded block of segments
// butted together, the chosen one filled. Its shape says that exactly one of them holds, which a row
// of separate buttons does not. Value is what is chosen, as with widget.Enum.
type choice struct {
	Value   string
	buttons []*widget.Clickable // One for each option, made once: Gio knows a control by its address.
}

func (c *choice) button(i int) *widget.Clickable {
	for len(c.buttons) <= i {
		c.buttons = append(c.buttons, new(widget.Clickable))
	}
	return c.buttons[i]
}

// Update takes the presses since the last frame, and says if one chose another option. A caller that
// writes what is chosen before it draws the block calls it first, as segments takes them only then.
func (c *choice) Update(gtx C, options []segment) bool {
	was := c.Value
	for i, option := range options {
		for c.button(i).Clicked(gtx) {
			c.Value = option.key
		}
	}
	return c.Value != was
}

// segmentRow is a run of options on one line of the block.
type segmentRow struct{ start, end int }

// packSegmentRows fills each line up to limit before starting the next. A line takes at least one
// segment, as one wider than the limit has nowhere else to go.
func packSegmentRows(widths []int, limit int) []segmentRow {
	if len(widths) == 0 {
		return nil
	}
	rows := []segmentRow{{}}
	used := 0
	for i, width := range widths {
		last := len(rows) - 1
		if i > rows[last].start && used+width > limit {
			rows[last].end = i
			rows = append(rows, segmentRow{start: i})
			used = 0
		}
		used += width
	}
	rows[len(rows)-1].end = len(widths)
	return rows
}

// balanceSegmentRows takes as few lines as the width allows, and then makes them as even as it can:
// filled to the edge, five options would leave one alone on a second line.
func balanceSegmentRows(widths []int, maxWidth int) []segmentRow {
	rows := packSegmentRows(widths, max(maxWidth, 1))
	if len(rows) <= 1 {
		return rows
	}
	low, total := 0, 0
	for _, width := range widths {
		low = max(low, width)
		total += width
	}
	best := rows
	for low <= total {
		limit := low + (total-low)/2
		if candidate := packSegmentRows(widths, limit); len(candidate) <= len(rows) {
			best = candidate
			total = limit - 1
		} else {
			low = limit + 1
		}
	}
	return best
}

// stretchSegmentRows widens every line to the widest, a pixel at a time, so the block is a rectangle.
func stretchSegmentRows(widths []int, rows []segmentRow) ([]int, int) {
	stretched := append([]int(nil), widths...)
	sums := make([]int, len(rows))
	block := 0
	for r, row := range rows {
		for i := row.start; i < row.end; i++ {
			sums[r] += widths[i]
		}
		block = max(block, sums[r])
	}
	for r, row := range rows {
		count := row.end - row.start
		share, remainder := (block-sums[r])/count, (block-sums[r])%count
		for i := row.start; i < row.end; i++ {
			stretched[i] += share
			if i-row.start < remainder {
				stretched[i]++
			}
		}
	}
	return stretched, block
}

// segments takes the presses on every option, and draws them as one block, on as many lines as the
// width needs.
func (a *App) segments(gtx C, c *choice, options []segment) D {
	c.Update(gtx, options)
	chosen := make([]bool, len(options))
	for i, option := range options {
		chosen[i] = c.Value == option.key
	}
	return a.segmentBlock(gtx, c, options, chosen)
}

// toggles is a block of segments any of which may hold at once, as the days of a week: a press turns
// its option on or off. on holds which are.
func (a *App) toggles(gtx C, c *choice, options []segment, on []bool) D {
	for i := range options {
		for c.button(i).Clicked(gtx) {
			on[i] = !on[i]
		}
	}
	return a.segmentBlock(gtx, c, options, on)
}

func (a *App) segmentBlock(gtx C, c *choice, options []segment, chosen []bool) D {
	if len(options) == 0 {
		return D{}
	}
	enabled := gtx.Source.Enabled()
	pad := gtx.Dp(segmentPad)
	widths := make([]int, len(options))
	filled := make([]bool, len(options))
	for i, option := range options {
		widths[i] = a.segmentWidth(gtx, option.words) + 2*pad
		over := a.hovered(gtx, c.button(i))
		filled[i] = chosen[i] || over
	}
	rows := balanceSegmentRows(widths, gtx.Constraints.Max.X)
	widths, block := stretchSegmentRows(widths, rows)
	height := gtx.Dp(segmentHeight)
	line := max(gtx.Dp(segmentLine), 1)
	size := image.Pt(block, height*len(rows))
	fade := func(shade color.NRGBA) color.NRGBA {
		if !enabled {
			shade.A = disabledOpacity
		}
		return shade
	}
	// The ground, the fills and the lines between in one rounded clip, so that a chosen segment at a
	// corner takes that corner.
	shape := clip.UniformRRect(image.Rectangle{Max: size}, gtx.Dp(segmentCorner))
	ground := shape.Push(gtx.Ops)
	paint.Fill(gtx.Ops, colorBackground)
	between := func(either bool) color.NRGBA {
		if either {
			return colorBackground
		}
		return colorBorder
	}
	for r, row := range rows {
		x, y := 0, r*height
		for i := row.start; i < row.end; i++ {
			if filled[i] {
				fill := colorQuiet
				if chosen[i] {
					fill = fade(colorPrimary)
				}
				paint.FillShape(gtx.Ops, fill, clip.Rect{Min: image.Pt(x, y), Max: image.Pt(x+widths[i], y+height)}.Op())
			}
			x += widths[i]
			if i < row.end-1 {
				paint.FillShape(gtx.Ops, between(filled[i] || filled[i+1]), clip.Rect{Min: image.Pt(x, y), Max: image.Pt(x+line, y+height)}.Op())
			}
		}
		if r < len(rows)-1 {
			paint.FillShape(gtx.Ops, colorBorder, clip.Rect{Min: image.Pt(0, y+height), Max: image.Pt(block, y+height+line)}.Op())
		}
	}
	ground.Pop()
	paint.FillShape(gtx.Ops, colorBorder, clip.Stroke{Path: shape.Path(gtx.Ops), Width: float32(line)}.Op())
	// The words last, each in the place its press is taken.
	for r, row := range rows {
		x, y := 0, r*height
		for i := row.start; i < row.end; i++ {
			moved := op.Offset(image.Pt(x, y)).Push(gtx.Ops)
			place := gtx
			place.Constraints = layout.Exact(image.Pt(widths[i], height))
			ink := colorText
			if chosen[i] {
				ink = colorStrong
			}
			button := c.button(i)
			button.Layout(place, func(gtx C) D {
				return layout.Center.Layout(gtx, a.segmentWords(options[i].words, fade(ink)))
			})
			a.watchHover(place, button, place.Constraints.Max)
			moved.Pop()
			x += widths[i]
		}
	}
	return D{Size: size}
}

func (a *App) segmentWords(words string, ink color.NRGBA) layout.Widget {
	style := a.label(sizeSmall, ink, words)
	style.Font.Weight = font.Medium
	style.MaxLines = 1
	return style.Layout
}

// How wide an option's words are, drawn into a macro that is left unused.
func (a *App) segmentWidth(gtx C, words string) int {
	free := gtx
	free.Constraints.Min = image.Point{}
	macro := op.Record(gtx.Ops)
	dims := a.segmentWords(words, colorText)(free)
	macro.Stop()
	return dims.Size.X
}
