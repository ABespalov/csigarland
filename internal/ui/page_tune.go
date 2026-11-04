// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tuning an effect, in a window of its own: its file, a preview here, on the garland or on disk.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"os"
	"strings"
	"time"

	"gioui.org/layout"
	"gioui.org/widget"
	"gioui.org/widget/material"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/engine"
)

type tunePage struct {
	id        widget.Editor
	name      widget.Editor
	file      widget.Editor
	choose    confirmClick
	toDisk    widget.Clickable
	onGarland widget.Clickable
	save      widget.Clickable
	close     confirmClick
	preview   *engine.Preview
	look      previewLook
	frame     []engine.Color
	checked   string
	problem   string
	clean     string // The file as last read or saved, to know whether closing would lose anything.
	current   currentChart
	// The file as its controls see it, read again whenever its words change, whoever changed them.
	model      *EffectModel
	fileColors []FileColor
	layers     [maxLayers]layerControls
	colors     colorEditor
	addLayer   widget.Clickable
	controls   widget.List
}

func (p *tunePage) init() {
	p.id = newField(true)
	p.name = newField(true)
	p.file = newField(false)
	p.frame = make([]engine.Color, previewLeds)
	p.controls.Axis = layout.Vertical
}

// open gets an effect's file and opens the window on it.
func (p *tunePage) open(a *App, id string) {
	a.withEffectFile(id, func(file []byte) {
		p.take(id, file)
		a.openModal(modalTune)
	})
}

func (p *tunePage) take(id string, file []byte) {
	p.id.SetText(id)
	p.colors.open = false
	p.preview = nil
	p.checked = ""
	p.setFile(string(file))
	p.clean = p.file.Text()
}

func (p *tunePage) dirty() bool { return p.file.Text() != p.clean }

// Closing what is not saved takes a second press.
func (p *tunePage) mayClose(now time.Time) bool {
	return !p.dirty() || p.close.press(now)
}

func (p *tunePage) setFile(text string) {
	p.file.SetText(text)
	p.recheck()
}

// The firmware's own parser says what is wrong, and only a change of the words asks it again.
func (p *tunePage) recheck() {
	text := p.file.Text()
	if text == p.checked {
		return
	}
	p.checked = text
	playing := p.preview != nil
	p.preview = nil
	p.problem = ""
	// The controls follow the words: a file the parser refuses can still be mended by them, while one
	// that is not JSON in this shape has none until it is again.
	p.model, p.fileColors = nil, nil
	if model, err := ReadEffect([]byte(text)); err == nil {
		p.model = model
		p.fileColors, _ = FileColors([]byte(text))
		// What a new algorithm reads is found now, as the preview starts again here anyway.
		for layer := range model.Layers() {
			algorithmUse(model.String(layer, "fx", ""))
		}
	}
	if text == "" {
		return
	}
	preview, err := engine.NewPreview([]byte(text))
	if err != nil {
		p.problem = err.Error()
		return
	}
	p.preview = preview
	// A change of its numbers shows better in motion than from the start again.
	if !playing {
		p.look.clock.restart()
	}
}

// newEffectFile is where a new effect starts: one layer of one colour, laid out as the built-in ones.
const newEffectFile = `{
  "format": "csigarland.effect/1",
  "name": "My effect",
  "layers": [
    {
      "fx": "solid",
      "col": [
        "FFAA50"
      ]
    }
  ]
}
`

// newEffect is the window on an effect of its own, to be made with the controls.
func (p *tunePage) newEffect(a *App) {
	p.take("my-effect", []byte(newEffectFile))
	a.openModal(modalTune)
}

// openFromDisk is the window with a file from this computer in it.
func (p *tunePage) openFromDisk(a *App) {
	a.openModal(modalTune)
	p.openFile(a)
}

func (p *tunePage) openFile(a *App) {
	if a.choosing {
		return
	}
	a.choosing = true
	asked := p.file.Text()
	owner := a.owner.Load()
	go func() {
		path, err := chooseFile(owner, "Effect file", "*.json", "Effect files (*.json)")
		var data []byte
		var id string
		var idErr error
		if err == nil && path != "" {
			if data, err = os.ReadFile(path); err != nil {
				err = fmt.Errorf("read the file: %w", err)
			}
			id, idErr = EffectIDFromPath(path)
		}
		a.post(func() {
			a.choosing = false
			if path == "" && err == nil {
				return
			}
			if err != nil {
				a.reportError(err)
				return
			}
			// The dialog does not hold the window: what was typed meanwhile is not to be lost either.
			if p.file.Text() != asked {
				a.reportError(errors.New("the file was changed while choosing, so nothing was opened: choose again"))
				return
			}
			p.take(id, data)
			if idErr != nil {
				a.reportError(idErr)
			}
		})
	}()
}

// saveToDisk takes the file as it stands, right or wrong, as a draft is worth keeping too.
func (p *tunePage) saveToDisk(a *App) {
	if a.choosing {
		return
	}
	a.choosing = true
	text := p.file.Text()
	name := strings.TrimSpace(p.id.Text())
	if name == "" {
		name = "effect"
	}
	owner := a.owner.Load()
	go func() {
		path, err := chooseSaveFile(owner, "Save the effect file", "*.json", "Effect files (*.json)", "json", name+".json")
		if err == nil && path != "" {
			if err = os.WriteFile(path, []byte(text), 0o644); err != nil {
				err = fmt.Errorf("write the file: %w", err)
			}
		}
		a.post(func() {
			a.choosing = false
			switch {
			case err != nil:
				a.reportError(err)
			case path != "":
				p.clean = text
				a.report("Saved to " + path)
			}
		})
	}()
}

func (p *tunePage) send(a *App, keep bool) {
	text := p.file.Text()
	id := strings.TrimSpace(p.id.Text())
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		if !keep {
			if err := client.PreviewEffect(ctx, []byte(text)); err != nil {
				return nil, err
			}
			return func() { a.report("Playing on the garland, with nothing saved, until another effect is chosen.") }, nil
		}
		if err := client.SaveEffect(ctx, id, []byte(text)); err != nil {
			return nil, err
		}
		return func() {
			p.clean = text
			a.report("Saved on the garland as " + id + ". It is in the Uploaded group.")
			a.effects.load(a)
		}, nil
	})
}

func (p *tunePage) body(a *App) layout.Widget {
	return func(gtx C) D {
		p.recheck()
		// Opening over what is not saved takes a second press, as closing does.
		if p.choose.click.Clicked(gtx) && (!p.dirty() || p.choose.press(gtx.Now)) {
			p.openFile(a)
		}
		if p.toDisk.Clicked(gtx) {
			p.saveToDisk(a)
		}
		if p.onGarland.Clicked(gtx) {
			p.send(a, false)
		}
		if p.save.Clicked(gtx) {
			p.send(a, true)
		}
		if p.close.click.Clicked(gtx) {
			a.closeModal(gtx.Now)
		}
		return layout.Flex{Axis: layout.Vertical}.Layout(gtx,
			layout.Rigid(func(gtx C) D {
				closeTip := ""
				if p.dirty() {
					closeTip = "Close, losing what is not saved"
				}
				return a.modalTitle(gtx, "Tune",
					func(gtx C) D {
						openTip := "Open a file"
						if p.dirty() {
							openTip = "Open a file, losing what is not saved"
						}
						return a.confirmLabelButton(gtx, &p.choose, buttonOther, "open", "Open", openTip, true)
					},
					func(gtx C) D {
						return a.labelButton(gtx, &p.toDisk, buttonOther, "save", "Save to file", "Save to a file on this computer", p.file.Text() != "")
					},
					func(gtx C) D {
						return a.labelButton(gtx, &p.onGarland, buttonOther, "bulb", "Preview on garland", "Play it on the garland, with nothing saved", a.client != nil && p.preview != nil)
					},
					func(gtx C) D {
						return a.labelButton(gtx, &p.save, buttonMain, "upload", "Save on garland", "", a.client != nil && p.preview != nil && strings.TrimSpace(p.id.Text()) != "")
					},
					func(gtx C) D {
						return a.confirmLabelButton(gtx, &p.close, buttonOther, "close", "Close", closeTip, true)
					},
				)
			}),
			layout.Rigid(a.modalStatus),
			layout.Rigid(layout.Spacer{Height: gap}.Layout),
			layout.Rigid(func(gtx C) D { return a.column(gtx, p.previewRows(a)...) }),
			layout.Rigid(layout.Spacer{Height: gap}.Layout),
			// The controls and the file below them scroll as one, under the LEDs that stay in sight.
			layout.Flexed(1, func(gtx C) D {
				seen := gtx.Constraints.Max.Y
				rows := append(p.controlRows(a), a.hint("The effect's file, as the controls write it"), func(gtx C) D {
					// All of its lines, so the page scrolls rather than the file, and never less than half
					// of what is in sight.
					gtx.Constraints.Min.Y = seen / 2
					return a.field(gtx, &p.file, "Choose Tune beside an effect, or open a file", 0)
				})
				return material.List(a.theme, &p.controls).Layout(gtx, len(rows), func(gtx C, i int) D {
					return layout.Inset{Bottom: gap, Right: gap}.Layout(gtx, rows[i])
				})
			}),
		)
	}
}

// The LEDs and how they play here, which stay in sight.
func (p *tunePage) previewRows(a *App) []layout.Widget {
	rows := []layout.Widget{
		func(gtx C) D { return a.layoutStrip(gtx, p.preview, &p.look, p.frame) },
		a.lookSliders(&p.look),
		a.hint(stripWords),
	}
	return append(rows, p.currentRows(a)...)
}

// Every field of the file as a control. The file below says the same in words, and either can be
// changed: the other follows.
func (p *tunePage) controlRows(a *App) []layout.Widget {
	state := a.hint("Each control writes its field in the file below, and the file can be typed in too: see the Effects Guide. A built-in effect's id replaces it until restored.")
	switch {
	case p.problem != "":
		state = a.statusLine(p.problem, true)
	case p.model == nil && p.file.Text() != "":
		state = a.statusLine("The file is not JSON in the shape of an effect, so the controls wait until it is.", true)
	}
	return append([]layout.Widget{state}, p.effectRows(a)...)
}
