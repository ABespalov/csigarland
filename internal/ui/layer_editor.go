// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tune's controls for every field of every layer, which read the file and write it.
// Contributors: None

package ui

import (
	"encoding/json"
	"fmt"
	"slices"
	"strconv"
	"strings"
	"sync"

	"gioui.org/layout"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/engine"
)

// The parser's own names, the algorithms in the order of the alphabet to be found by eye.
var (
	algorithmNames = sync.OnceValue(func() []string {
		names := slices.Clone(engine.Algorithms())
		slices.Sort(names)
		return names
	})
	figurineNames = sync.OnceValue(engine.Figurines)
)

const (
	byteMax        = 255
	defaultLayerOn = 255
	ledIndexMax    = 65535
	rotateMax      = 32767
)

var (
	paletteKinds = []segment{{"colours", "From colours"}, {"rainbow", "Rainbow"}, {"gradient", "Gradient"}}
	zoneKinds    = []segment{{"all", "Everywhere"}, {"figurines", "Figurines"}, {"every", "Every few"}}
	blendKinds   = []segment{{"replace", "Replace"}, {"add", "Add"}, {"max", "Brighter"}}
)

// layerControls are one layer's, kept where they are: Gio knows a control by its address.
type layerControls struct {
	remove                                   confirmClick
	fxOpen                                   widget.Clickable
	fxShown                                  bool
	fxFilter                                 widget.Editor
	fxPicks                                  []widget.Clickable
	sx, ix, bri                              widget.Float
	start, stop, grp, spc, of, every, offset numberField
	rev, mi                                  widget.Bool
	blend, palette, zone                     choice
	figurines                                []widget.Bool
	cols, stopLine                           colourLine
	addColor                                 widget.Clickable

	places     [maxPaletteStops]numberField
	addStop    widget.Clickable
	dropUnused [2]widget.Clickable // For col, and for pal.
}

// write takes a file the controls made, as if it had been typed.
func (p *tunePage) write(file []byte) { p.setFile(string(file)) }

// layerEditor is every control of one layer, in a block of its own.
func (p *tunePage) layerEditor(a *App, layer int) layout.Widget {
	c := &p.layers[layer]
	return func(gtx C) D {
		if p.model == nil || layer >= p.model.Layers() {
			return D{}
		}
		if c.remove.confirmed(gtx) {
			p.colors.open = false
			p.write(p.model.RemoveLayer(layer))
			return D{}
		}
		title := fmt.Sprintf("Layer %d", layer+1)
		if p.model.Layers() > 1 && layer == 0 {
			title += ", drawn first"
		}
		header := func(gtx C) D {
			tools := []layout.FlexChild{layout.Flexed(1, a.heading(title))}
			if p.model.Layers() > 1 {
				tools = append(tools, layout.Rigid(func(gtx C) D {
					return a.confirmButton(gtx, &c.remove, buttonLosing, "trash", "Remove this layer", true)
				}))
			}
			return layout.Flex{Alignment: layout.Middle}.Layout(gtx, tools...)
		}
		rows := []layout.Widget{header, p.algorithmRow(a, layer)}
		// Only what changes the light of this algorithm has a control.
		fx := p.model.String(layer, "fx", "")
		use := algorithmUse(fx)
		var sliders []layout.Widget
		var unused []string
		if use.sx {
			sliders = append(sliders, p.byteSlider(a, layer, "sx", "Pace, sx", &c.sx, defaultLayerValue))
		} else {
			unused = append(unused, "pace, sx")
		}
		if use.ix {
			sliders = append(sliders, p.byteSlider(a, layer, "ix", "Intensity, ix", &c.ix, defaultLayerValue))
		} else {
			unused = append(unused, "intensity, ix")
		}
		sliders = append(sliders, p.byteSlider(a, layer, "bri", "Brightness, bri", &c.bri, defaultLayerOn))
		rows = append(rows, a.sliderPairs(sliders...))
		if len(unused) > 0 {
			rows = append(rows, a.hint(fx+" does not use "+strings.Join(unused, " or ")+"."))
		}
		rows = append(rows, p.colorRow(a, layer), p.paletteRows(a, layer), p.rangeRow(a, layer), p.flagsRow(a, layer),
			p.zoneRows(a, layer))
		// The first layer is drawn over dark, where every blend is the same.
		if layer > 0 || p.model.Raw(layer, "blend") != nil {
			rows = append(rows, func(gtx C) D { return p.blendRow(a, gtx, layer) })
		}
		// A row that wrote the file has the model read again, and a file left wrong has none.
		for i, draw := range rows {
			rows[i] = func(gtx C) D {
				if p.model == nil || layer >= p.model.Layers() {
					return D{}
				}
				return draw(gtx)
			}
		}
		return a.section(gtx, rows...)
	}
}

// section is a block within a window's panel, a shade darker than it.
func (a *App) section(gtx C, rows ...layout.Widget) D {
	return layout.Stack{}.Layout(gtx,
		layout.Expanded(func(gtx C) D {
			fill(gtx, gtx.Constraints.Min, gtx.Dp(corner), colorSurface)
			return D{Size: gtx.Constraints.Min}
		}),
		layout.Stacked(func(gtx C) D {
			gtx.Constraints.Min.X = gtx.Constraints.Max.X
			return layout.UniformInset(gapLarge).Layout(gtx, func(gtx C) D { return a.column(gtx, rows...) })
		}),
	)
}

// A field of 0 to 255 on a slider.
func (p *tunePage) byteSlider(a *App, layer int, key, name string, f *widget.Float, fallback int) layout.Widget {
	return func(gtx C) D {
		value := p.model.Int(layer, key, fallback)
		if f.Update(gtx) {
			value = int(f.Value*byteMax + 0.5)
			p.write(p.model.SetField(layer, key, value))
		} else if !f.Dragging() {
			f.Value = float32(value) / byteMax
		}
		return a.slider(name, f, strconv.Itoa(value))(gtx)
	}
}

func (p *tunePage) algorithmRow(a *App, layer int) layout.Widget {
	c := &p.layers[layer]
	return func(gtx C) D {
		names := algorithmNames()
		if len(c.fxPicks) != len(names) {
			c.fxPicks = make([]widget.Clickable, len(names))
			c.fxFilter = newField(true)
		}
		current := p.model.String(layer, "fx", "")
		if c.fxOpen.Clicked(gtx) {
			c.fxShown = !c.fxShown
		}
		rows := []layout.Widget{func(gtx C) D {
			return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
				layout.Rigid(a.label(sizeSmall, colorMuted, "Algorithm, fx").Layout),
				layout.Rigid(layout.Spacer{Width: gap}.Layout),
				layout.Rigid(func(gtx C) D {
					return a.button(gtx, &c.fxOpen, buttonOther, current, "Choose the algorithm: see the Effects Guide", true)
				}),
			)
		}}
		if c.fxShown {
			words := strings.ToLower(strings.TrimSpace(c.fxFilter.Text()))
			var picks []layout.Widget
			for i, name := range names {
				if c.fxPicks[i].Clicked(gtx) {
					c.fxShown = false
					p.write(p.model.SetField(layer, "fx", name))
					return D{}
				}
				if words != "" && !strings.Contains(name, words) {
					continue
				}
				kind := buttonOther
				if name == current {
					kind = buttonMain
				}
				picks = append(picks, func(gtx C) D { return a.button(gtx, &c.fxPicks[i], kind, name, "", true) })
			}
			rows = append(rows,
				func(gtx C) D { return a.field(gtx, &c.fxFilter, "Type to find an algorithm", 0) },
				func(gtx C) D { return row(gtx, picks...) })
		}
		return a.column(gtx, rows...)
	}
}

func (p *tunePage) rangeRow(a *App, layer int) layout.Widget {
	c := &p.layers[layer]
	field := func(name, key string, f *numberField, fallback, low, high int) layout.Widget {
		return a.namedStepper(name, f, p.model.Int(layer, key, fallback), low, high, func(value int) {
			p.write(p.model.SetField(layer, key, value))
		})
	}
	return func(gtx C) D {
		return row(gtx,
			field("From LED, start", "start", &c.start, 0, 0, ledIndexMax),
			// stop is the first LED left out, as the preview numbers them.
			field("Up to LED, not lit, stop (0: the end)", "stop", &c.stop, 0, 0, ledIndexMax),
			field("LEDs per group, grp", "grp", &c.grp, 1, 1, byteMax),
			field("Skipped between, spc", "spc", &c.spc, 0, 0, byteMax),
			field("Turned by, of", "of", &c.of, 0, -rotateMax-1, rotateMax),
		)
	}
}

// A flag is written only when it is on, as off is what a layer is without it.
func (p *tunePage) flagsRow(a *App, layer int) layout.Widget {
	c := &p.layers[layer]
	flag := func(key, words string, b *widget.Bool) layout.Widget {
		return func(gtx C) D {
			b.Value = p.model.Bool(layer, key)
			if b.Update(gtx) {
				if b.Value {
					p.write(p.model.SetField(layer, key, true))
				} else {
					p.write(p.model.DeleteField(layer, key))
				}
			}
			return a.checkbox(gtx, b, words)
		}
	}
	return func(gtx C) D {
		return row(gtx, flag("rev", "Reverse, rev", &c.rev), flag("mi", "Mirror, mi", &c.mi))
	}
}

func (p *tunePage) zoneRows(a *App, layer int) layout.Widget {
	c := &p.layers[layer]
	return func(gtx C) D {
		raw := p.model.Raw(layer, "zone")
		kind := "all"
		var listed []string
		var every zoneEvery
		switch rawKind(raw) {
		case '[':
			kind = "figurines"
			_ = json.Unmarshal(raw, &listed)
		case '{':
			kind = "every"
			every = readZoneEvery(raw)
		}
		c.zone.Value = kind
		rows := []layout.Widget{func(gtx C) D {
			return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
				layout.Rigid(a.label(sizeSmall, colorMuted, "Zone").Layout),
				layout.Rigid(layout.Spacer{Width: gap}.Layout),
				layout.Rigid(func(gtx C) D { return a.segments(gtx, &c.zone, zoneKinds) }),
			)
		}}
		// The presses first, as for the palette.
		if c.zone.Update(gtx, zoneKinds) {
			switch c.zone.Value {
			case "all":
				p.write(p.model.DeleteField(layer, "zone"))
			case "figurines":
				p.write(p.model.SetField(layer, "zone", figurineNames()[:1]))
			default:
				p.write(p.model.SetField(layer, "zone", map[string]int{"every": 2, "offset": 0}))
			}
			return D{}
		}
		switch kind {
		case "figurines":
			rows = append(rows, p.figurineRow(a, layer, listed))
		case "every":
			setEvery := func(n, k int) {
				p.write(p.model.SetField(layer, "zone", map[string]int{"every": n, "offset": min(k, n-1)}))
			}
			rows = append(rows, func(gtx C) D {
				return row(gtx,
					a.namedStepper("Every", &c.every, every.Every, 1, byteMax, func(n int) { setEvery(n, every.Offset) }),
					a.namedStepper("From", &c.offset, every.Offset, 0, max(every.Every-1, 0), func(k int) { setEvery(every.Every, k) }),
				)
			})
		}
		return a.column(gtx, rows...)
	}
}

// The figurines a zone lists, in the layout's order. The last one ticked stays: a zone lists some.
func (p *tunePage) figurineRow(a *App, layer int, listed []string) layout.Widget {
	c := &p.layers[layer]
	return func(gtx C) D {
		names := figurineNames()
		if len(c.figurines) != len(names) {
			c.figurines = make([]widget.Bool, len(names))
		}
		items := make([]layout.Widget, len(names))
		for i, name := range names {
			b := &c.figurines[i]
			b.Value = slices.Contains(listed, name)
			if b.Update(gtx) {
				var next []string
				for j, other := range names {
					if (j == i && b.Value) || (j != i && slices.Contains(listed, other)) {
						next = append(next, other)
					}
				}
				if len(next) > 0 {
					p.write(p.model.SetField(layer, "zone", next))
				}
				return D{}
			}
			items[i] = func(gtx C) D { return a.checkbox(gtx, b, name) }
		}
		return row(gtx, items...)
	}
}

// Replace is what a layer does with no blend, so it is written only when it is another.
func (p *tunePage) blendRow(a *App, gtx C, layer int) D {
	c := &p.layers[layer]
	kind := p.model.String(layer, "blend", "replace")
	c.blend.Value = kind
	if c.blend.Update(gtx, blendKinds) {
		if c.blend.Value == "replace" {
			p.write(p.model.DeleteField(layer, "blend"))
		} else {
			p.write(p.model.SetField(layer, "blend", c.blend.Value))
		}
		return D{}
	}
	return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
		layout.Rigid(a.label(sizeSmall, colorMuted, "Over the layers below, blend").Layout),
		layout.Rigid(layout.Spacer{Width: gap}.Layout),
		layout.Rigid(func(gtx C) D { return a.segments(gtx, &c.blend, blendKinds) }),
	)
}

// The name, kept to what the parser takes, and the way to a further layer.
func (p *tunePage) effectRows(a *App) []layout.Widget {
	rows := []layout.Widget{func(gtx C) D {
		if p.model != nil {
			if !gtx.Focused(&p.name) && p.name.Text() != p.model.Name() {
				p.name.SetText(p.model.Name())
			}
			for {
				seen, ok := p.name.Update(gtx)
				if !ok {
					break
				}
				if _, changed := seen.(widget.ChangeEvent); changed && p.name.Text() != p.model.Name() && validEffectName(p.name.Text()) {
					p.write(p.model.SetName(p.name.Text()))
				}
			}
		}
		return a.sliderPairs(
			func(gtx C) D {
				return a.labelled(gtx, "Id, 1 to 32 lowercase letters, digits or hyphens", &p.id, "my-effect")
			},
			func(gtx C) D { return a.labelled(gtx, "Name, 1 to 32 characters, no quotes", &p.name, "My effect") },
		)(gtx)
	}}
	if p.model == nil {
		return rows
	}
	// The firmware takes four, and so many have controls. A file with more is still read, to be
	// mended in its text.
	if p.model.Layers() > maxLayers {
		rows = append(rows, a.statusLine(fmt.Sprintf("The file has %d layers, and the firmware takes %d: take the rest out of the file below. Only the first %d have controls.",
			p.model.Layers(), maxLayers, maxLayers), true))
	}
	for layer := range min(p.model.Layers(), maxLayers) {
		rows = append(rows, p.layerEditor(a, layer))
	}
	if p.model.Layers() < maxLayers {
		rows = append(rows, func(gtx C) D {
			if p.addLayer.Clicked(gtx) {
				p.write(p.model.AddLayer())
			}
			return a.labelButton(gtx, &p.addLayer, buttonOther, "add", "Add a layer", "A layer drawn over those before it", true)
		})
	}
	return rows
}

func validEffectName(name string) bool {
	if name == "" || len(name) > maxEffectIDBytes {
		return false
	}
	for _, b := range []byte(name) {
		if b < 0x20 || b > 0x7E || b == '"' || b == '\\' {
			return false
		}
	}
	return true
}

// A field with its words above it.
func (a *App) labelled(gtx C, words string, editor *widget.Editor, hint string) D {
	return a.column(gtx, a.hint(words), func(gtx C) D { return a.field(gtx, editor, hint, 0) })
}
