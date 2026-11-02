// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tune's controls for a layer's colours and its palette, and the picker on one of them.
// Contributors: None

package ui

import (
	"encoding/json"
	"fmt"
	"slices"

	"gioui.org/layout"
	"gioui.org/unit"
)

// As wide as the picker and its title need.
const (
	pickerWindowWide = unit.Dp(560)
	colorItemGap     = unit.Dp(20) // Beside the row's own gap, so a colour's parts stand apart from the next.
)

// ownPalette is whether a layer has a palette written, rather than one made of its colours.
func (p *tunePage) ownPalette(layer int) bool { return p.model.Raw(layer, "pal") != nil }

// A field the algorithm does not read: said once, with a way to take it out, and nothing at all when
// it is not in the file.
func (p *tunePage) unusedRow(a *App, layer int, key, words string) layout.Widget {
	c := &p.layers[layer]
	drop := &c.dropUnused[0]
	if key == "pal" {
		drop = &c.dropUnused[1]
	}
	return func(gtx C) D {
		if p.model.Raw(layer, key) == nil {
			return D{}
		}
		if drop.Clicked(gtx) {
			p.colors.open = false
			p.write(p.model.DeleteField(layer, key))
			return D{}
		}
		return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
			layout.Flexed(1, a.hint(words)),
			layout.Rigid(func(gtx C) D {
				return a.iconButton(gtx, drop, buttonLosing, "trash", "Take "+key+" out of the file", true)
			}),
		)
	}
}

// The colours of col, as many as there are and no more than the algorithm reads: a layer with none
// has its algorithm's warm white.
func (p *tunePage) colorRow(a *App, layer int) layout.Widget {
	c := &p.layers[layer]
	return func(gtx C) D {
		fx := p.model.String(layer, "fx", "")
		use := algorithmUse(fx)
		read := use.Colours(p.ownPalette(layer))
		if read == 0 {
			words := "Colours, col: " + fx + " does not use them."
			if use.col > 0 {
				words = "Colours, col: " + fx + " does not use them while the palette is its own."
			}
			return p.unusedRow(a, layer, "col", words)(gtx)
		}
		cols := p.layerColors(layer, "col")
		if c.addColor.Clicked(gtx) && len(cols) < read {
			hexes := colorHexes(cols)
			next := "FFFFFF"
			if len(hexes) > 0 {
				next = hexes[len(hexes)-1]
			}
			p.write(p.model.SetField(layer, "col", append(hexes, next)))
			return D{}
		}
		var tail []layout.Widget
		if len(cols) == 0 {
			tail = append(tail, a.hint("None: the algorithm's own warm white."))
		}
		if len(cols) < read {
			tail = append(tail, func(gtx C) D { return a.iconButton(gtx, &c.addColor, buttonOther, "add", "Add a colour", true) })
		}
		line := func(gtx C) D {
			return a.colourLine(gtx, &c.cols, len(cols), func(i int) RGB { return cols[i].Color }, nil,
				"Pick this colour, or drag it before or after another", tail,
				func(i int) { p.colors.choose(cols[i]) },
				func(from, to int) { p.write(p.model.SetField(layer, "col", moveItem(colorHexes(cols), from, to))) })
		}
		rows := []layout.Widget{func(gtx C) D {
			return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
				layout.Rigid(a.label(sizeSmall, colorMuted, "Colours, col").Layout),
				layout.Rigid(layout.Spacer{Width: colorItemGap}.Layout),
				layout.Flexed(1, line),
			)
		}}
		// One more than it reads is left to be picked and removed, and said to change nothing.
		if len(cols) > read {
			rows = append(rows, a.hint(fmt.Sprintf("%s reads only the first %s here: the rest change nothing. Pick one to remove it.", fx, colourCount(read))))
		}
		return a.column(gtx, rows...)
	}
}

func colourCount(n int) string {
	if n == 1 {
		return "colour"
	}
	return fmt.Sprintf("%d colours", n)
}

// pickerWindow is the picker on the colour chosen, over Tune, writing the colour in its own place in
// the file. It closes by its button, by Escape, and once the colour is no longer in the file.
func (p *tunePage) pickerWindow(a *App) layout.Widget {
	return func(gtx C) D {
		current, found := p.colorNow()
		if !found || p.colors.done.Clicked(gtx) {
			p.colors.open = false
			return D{}
		}
		removable := current.Field == "col" || len(p.layerColors(current.Layer, "pal")) > minPaletteStops
		if p.colors.remove.confirmed(gtx) && removable {
			p.colors.open = false
			p.removeColor(current)
			return D{}
		}
		title := fmt.Sprintf("Colour %d of layer %d", current.Index+1, current.Layer+1)
		what := "Remove this colour"
		if current.Field == "pal" {
			title = fmt.Sprintf("Stop %d of layer %d's gradient, at %d", current.Index+1, current.Layer+1, current.Stop)
			what = "Remove this stop"
		}
		gtx.Constraints.Max.X = min(gtx.Constraints.Max.X, gtx.Dp(pickerWindowWide))
		gtx.Constraints.Min.X = gtx.Constraints.Max.X
		return a.column(gtx,
			func(gtx C) D {
				return a.modalTitle(gtx, title,
					func(gtx C) D {
						return a.confirmLabelButton(gtx, &p.colors.remove, buttonLosing, "trash", "Remove", what, removable)
					},
					func(gtx C) D { return a.labelButton(gtx, &p.colors.done, buttonMain, "close", "Done", "", true) },
				)
			},
			func(gtx C) D {
				return a.colorPicker(gtx, &p.colors, current.Color, func(next RGB) {
					p.write(SetFileColor([]byte(p.file.Text()), current, next))
				})
			},
		)
	}
}

// removeColor takes a colour out of col, and col with its last, or a stop out of the gradient.
func (p *tunePage) removeColor(at FileColor) {
	if at.Field == "col" {
		hexes := colorHexes(p.layerColors(at.Layer, "col"))
		if at.Index >= len(hexes) {
			return
		}
		if hexes = slices.Delete(hexes, at.Index, at.Index+1); len(hexes) == 0 {
			p.write(p.model.DeleteField(at.Layer, "col"))
		} else {
			p.write(p.model.SetField(at.Layer, "col", hexes))
		}
		return
	}
	stops := p.layerColors(at.Layer, "pal")
	if at.Index >= len(stops) || len(stops) <= minPaletteStops {
		return
	}
	stops = slices.Delete(stops, at.Index, at.Index+1)
	p.write(p.model.SetField(at.Layer, "pal", gradientOf(stops)))
}

// appendStop puts a stop after the last, with the last one's colour, so that every stop before it
// keeps its number, colour and place. It goes at 255, and a last one already there moves halfway back
// to the one before it. There is no room when those two stand side by side.
func appendStop(places []int, hexes []string) ([]int, []string, bool) {
	n := len(places)
	if n < minPaletteStops || n != len(hexes) {
		return places, hexes, false
	}
	moved := slices.Clone(places)
	if moved[n-1] == byteMax {
		if byteMax-moved[n-2] < 2 {
			return places, hexes, false
		}
		moved[n-1] = (moved[n-2] + byteMax) / 2
	}
	return append(moved, byteMax), append(slices.Clone(hexes), hexes[n-1]), true
}

// The pal of stops, each its place and its colour.
func gradientOf(stops []FileColor) []any {
	pal := make([]any, 0, 2*len(stops))
	for _, stop := range stops {
		pal = append(pal, stop.Stop, stop.Color.Hex())
	}
	return pal
}

func (p *tunePage) colorNow() (FileColor, bool) {
	for _, color := range p.fileColors {
		if color.Same(p.colors.at) {
			return color, true
		}
	}
	return FileColor{}, false
}

func (p *tunePage) layerColors(layer int, field string) []FileColor {
	var found []FileColor
	for _, color := range p.fileColors {
		if color.Layer == layer && color.Field == field {
			found = append(found, color)
		}
	}
	return found
}

func colorHexes(colors []FileColor) []string {
	hexes := make([]string, len(colors))
	for i, color := range colors {
		hexes[i] = color.Color.Hex()
	}
	return hexes
}

// The kind of a field's value: what its first character says it is.
func rawKind(raw json.RawMessage) byte {
	for _, b := range raw {
		if !isSpace(b) {
			return b
		}
	}
	return 0
}

func (p *tunePage) paletteRows(a *App, layer int) layout.Widget {
	c := &p.layers[layer]
	return func(gtx C) D {
		fx := p.model.String(layer, "fx", "")
		if !algorithmUse(fx).pal {
			return p.unusedRow(a, layer, "pal", "Palette, pal: "+fx+" does not use one.")(gtx)
		}
		kind := "colours"
		switch rawKind(p.model.Raw(layer, "pal")) {
		case '"':
			kind = "rainbow"
		case '[':
			kind = "gradient"
		}
		c.palette.Value = kind
		// The presses first: the block below shows what the file says, once it says it.
		if c.palette.Update(gtx, paletteKinds) {
			p.colors.open = false
			switch c.palette.Value {
			case "colours":
				p.write(p.model.DeleteField(layer, "pal"))
			case "rainbow":
				p.write(p.model.SetField(layer, "pal", "Rainbow"))
			default:
				first, last := "FFAA50", "000000"
				if hexes := colorHexes(p.layerColors(layer, "col")); len(hexes) > 0 {
					first, last = hexes[0], hexes[len(hexes)-1]
				}
				p.write(p.model.SetField(layer, "pal", []any{0, first, byteMax, last}))
			}
			return D{}
		}
		rows := []layout.Widget{func(gtx C) D {
			return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
				layout.Rigid(a.label(sizeSmall, colorMuted, "Palette, pal").Layout),
				layout.Rigid(layout.Spacer{Width: gap}.Layout),
				layout.Rigid(func(gtx C) D { return a.segments(gtx, &c.palette, paletteKinds) }),
			)
		}}
		if kind == "gradient" {
			rows = append(rows, a.hint("Its stops: each is a colour, and where it stands along the gradient, from 0 to 255. Press a colour to change it, or drag it before or after another."),
				p.stopsRow(a, layer))
		}
		return a.column(gtx, rows...)
	}
}

// A gradient's stops, each a colour over its place, 0 to 255 and in order.
func (p *tunePage) stopsRow(a *App, layer int) layout.Widget {
	c := &p.layers[layer]
	return func(gtx C) D {
		stops := p.layerColors(layer, "pal")
		rewrite := func(places []int, hexes []string) {
			pal := make([]any, 0, 2*len(places))
			for i := range places {
				pal = append(pal, places[i], hexes[i])
			}
			p.write(p.model.SetField(layer, "pal", pal))
		}
		places := make([]int, len(stops))
		for i, stop := range stops {
			places[i] = stop.Stop
		}
		hexes := colorHexes(stops)
		longer, longerHexes, room := appendStop(places, hexes)
		room = room && len(stops) < maxPaletteStops
		if c.addStop.Clicked(gtx) && room {
			rewrite(longer, longerHexes)
			return D{}
		}
		// Each place beside its colour. A colour dragged elsewhere leaves the places as they are, in
		// order, and takes the place of where it lands.
		place := func(i int) layout.Widget {
			low, high := 0, byteMax
			if i > 0 {
				low = places[i-1] + 1
			}
			if i < len(stops)-1 {
				high = places[i+1] - 1
			}
			return func(gtx C) D {
				return a.numberStepper(gtx, &c.places[i], places[i], low, high, func(place int) {
					moved := slices.Clone(places)
					moved[i] = place
					rewrite(moved, hexes)
				})
			}
		}
		var tail []layout.Widget
		if len(stops) < maxPaletteStops {
			tip := "Add a colour after the last"
			if !room {
				tip = "No room after the last two: move one of them back first"
			}
			tail = append(tail, func(gtx C) D { return a.iconButton(gtx, &c.addStop, buttonOther, "add", tip, room) })
		}
		return a.colourLine(gtx, &c.stopLine, len(stops), func(i int) RGB { return stops[i].Color }, place,
			"Pick this colour, or drag it before or after another", tail,
			func(i int) { p.colors.choose(stops[i]) },
			func(from, to int) { rewrite(places, moveItem(hexes, from, to)) })
	}
}
