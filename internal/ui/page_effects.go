// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Effects page: what plays now, and every effect by group.
// Contributors: None

package ui

import (
	"context"
	"fmt"

	"gioui.org/layout"
	"gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"

	"github.com/ABespalov/csigarland/internal/api"
)

type effectRow struct {
	favorite widget.Bool
	play     widget.Clickable
	show     widget.Clickable
	tune     widget.Clickable
	remove   confirmClick
}

type effectsPage struct {
	list       []api.Effect
	store      api.EffectStore
	rowsOf     map[string]*effectRow
	opened     map[string]*groupState
	power      widget.Clickable
	newEffect  widget.Clickable
	openFile   widget.Clickable
	brightness widget.Float
	speed      widget.Float
	wanted     map[string]float64
	sending    bool
	changes    int
	commands   int // Those on their way, whose outcome a state read meanwhile knows nothing of.
	playlist   playlistCard
}

type groupState struct {
	toggle widget.Clickable
	open   bool
	known  bool
}

func (p *effectsPage) init() {
	p.rowsOf = map[string]*effectRow{}
	p.opened = map[string]*groupState{}
	p.wanted = map[string]float64{}
}

// All of the page in one turn, as the others load. Its places for uploads are only a remark on it.
func (p *effectsPage) load(a *App) {
	p.loadPlaylist(a)
	changes, pushes := p.changes, a.pushes
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		state, err := client.State(ctx)
		if err != nil {
			return nil, err
		}
		effects, err := client.Effects(ctx)
		if err != nil {
			return nil, err
		}
		store, storeErr := client.EffectStore(ctx)
		return func() {
			p.list = effects
			if storeErr == nil {
				p.store = store
			}
			if changes == p.changes && p.commands == 0 && pushes == a.pushes {
				a.takeState(state)
			} else {
				p.markActive(a.state.Effect)
			}
		}, nil
	})
}

func (p *effectsPage) markActive(id string) {
	for i := range p.list {
		p.list[i].Active = p.list[i].ID == id
	}
}

func (p *effectsPage) favorites() int {
	count := 0
	for i := range p.list {
		if p.list[i].Favorite {
			count++
		}
	}
	return count
}

// The built-in effects stand in for the garland's list when there is none to be had, to tune and to
// preview with no garland at hand.
func (p *effectsPage) showsLibrary(a *App) bool {
	return a.local || (a.connected && len(p.list) == 0 && a.online == stateOffline)
}

func (p *effectsPage) rows(a *App) []layout.Widget {
	local := p.showsLibrary(a)
	source := p.list
	var rows []layout.Widget
	if local {
		source = libraryEffects()
	} else {
		rows = append(rows, p.nowCard(a))
	}
	rows = append(rows, p.toolsCard(a, local))
	if !local && p.playlist.read {
		rows = append(rows, p.playlistWidget(a))
	}
	groups := GroupEffects(source)
	for g := range groups {
		group := groups[g]
		if local && len(group.Effects) == 0 {
			continue
		}
		state := p.opened[group.Name]
		if state == nil {
			state = &groupState{}
			p.opened[group.Name] = state
		}
		if !state.known && len(source) > 0 {
			state.known = true
			for _, effect := range group.Effects {
				state.open = state.open || effect.Favorite || effect.Active || group.Name == UploadedGroup
			}
		}
		rows = append(rows, p.groupTitle(a, group, state))
		if !state.open {
			continue
		}
		if len(group.Effects) == 0 {
			rows = append(rows, a.hint("None yet. Make a new effect or open a file above, and save it on the garland."))
		}
		for i := range group.Effects {
			rows = append(rows, p.effectRowWidget(a, group.Effects[i], local))
		}
	}
	return rows
}

func (p *effectsPage) toolsCard(a *App, local bool) layout.Widget {
	words := "A new effect, or a file from this computer, is tuned here, and saved here or on the garland."
	if !local && p.store.Max > 0 {
		words += fmt.Sprintf(" %d of its %d places for uploads are taken.", p.store.Uploaded, p.store.Max)
	}
	if local {
		words = "With no garland: these are the built-in effects. Tune and preview them, a new effect or a file from this computer, and save the result to a file."
	}
	return func(gtx C) D {
		if p.newEffect.Clicked(gtx) {
			a.tune.newEffect(a)
		}
		if p.openFile.Clicked(gtx) {
			a.tune.openFromDisk(a)
		}
		return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
			layout.Rigid(func(gtx C) D {
				return a.labelButton(gtx, &p.newEffect, buttonMain, "add", "New effect", "Make an effect of your own in Tune", true)
			}),
			layout.Rigid(layout.Spacer{Width: gap}.Layout),
			layout.Rigid(func(gtx C) D { return a.labelButton(gtx, &p.openFile, buttonOther, "open", "Open a file", "", true) }),
			layout.Rigid(layout.Spacer{Width: gap}.Layout),
			layout.Flexed(1, a.hint(words)),
		)
	}
}

func (p *effectsPage) groupTitle(a *App, group EffectGroup, state *groupState) layout.Widget {
	return func(gtx C) D {
		if state.toggle.Clicked(gtx) {
			state.open = !state.open
		}
		mark := "+ "
		if state.open {
			mark = "- "
		}
		return state.toggle.Layout(gtx, func(gtx C) D {
			gtx.Constraints.Min.X = gtx.Constraints.Max.X
			return layout.Inset{Top: gapSmall, Bottom: gapSmall}.Layout(gtx, a.heading(mark+GroupTitle(group)))
		})
	}
}

func (p *effectsPage) find(id string) *api.Effect {
	for i := range p.list {
		if p.list[i].ID == id {
			return &p.list[i]
		}
	}
	return nil
}

func (p *effectsPage) effectRowWidget(a *App, listed api.Effect, local bool) layout.Widget {
	id := listed.ID
	widgets := p.rowsOf[id]
	if widgets == nil {
		widgets = &effectRow{}
		p.rowsOf[id] = widgets
	}
	return func(gtx C) D {
		effect := &listed
		if !local {
			if effect = p.find(id); effect == nil {
				return D{}
			}
			widgets.favorite.Value = effect.Favorite
			if widgets.favorite.Update(gtx) {
				p.setFavorite(a, id, widgets.favorite.Value)
			}
			if widgets.play.Clicked(gtx) {
				p.play(a, id)
			}
			if widgets.remove.confirmed(gtx) {
				p.remove(a, id)
			}
		}
		if widgets.show.Clicked(gtx) {
			a.preview.open(a, id, effect.Name)
		}
		if widgets.tune.Clicked(gtx) {
			a.tune.open(a, id)
		}
		return layout.Inset{Left: gapLarge}.Layout(gtx, func(gtx C) D {
			name := a.label(sizeBody, colorText, effect.Name)
			children := []layout.FlexChild{}
			if !local {
				children = append(children, layout.Rigid(func(gtx C) D {
					style := material.CheckBox(a.theme, &widgets.favorite, "")
					style.Size = unit.Dp(20)
					style.IconColor = colorBrand
					return style.Layout(gtx)
				}))
			}
			if effect.Active {
				name.Color = colorActive
			}
			children = append(children,
				layout.Flexed(1, name.Layout),
				layout.Rigid(func(gtx C) D { return a.iconButton(gtx, &widgets.show, buttonOther, "preview", "Preview here", true) }),
				layout.Rigid(layout.Spacer{Width: gapSmall}.Layout),
				layout.Rigid(func(gtx C) D { return a.iconButton(gtx, &widgets.tune, buttonOther, "tune", "Tune", true) }),
			)
			if !local {
				kind, tip := buttonOther, "Play on the garland"
				if effect.Active {
					kind, tip = buttonMain, "Playing on the garland now"
				}
				children = append(children,
					layout.Rigid(layout.Spacer{Width: gapSmall}.Layout),
					layout.Rigid(func(gtx C) D { return a.iconButton(gtx, &widgets.play, kind, "play", tip, true) }))
			}
			if !local && effect.Uploaded {
				kind, glyph, tip := buttonLosing, "trash", "Delete from the garland"
				if effect.BuiltIn {
					kind, glyph, tip = buttonOther, "restore", "Go back to the built-in effect"
				}
				children = append(children,
					layout.Rigid(layout.Spacer{Width: gapSmall}.Layout),
					layout.Rigid(func(gtx C) D { return a.confirmButton(gtx, &widgets.remove, kind, glyph, tip, true) }))
			}
			return layout.Flex{Alignment: layout.Middle}.Layout(gtx, children...)
		})
	}
}

func (p *effectsPage) setFavorite(a *App, id string, favorite bool) {
	effect := p.find(id)
	if effect == nil {
		return
	}
	effect.Favorite = favorite
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		if err := client.SetFavorite(ctx, id, favorite); err != nil {
			// The garland refuses to lose its last favourite, so show what it has.
			return func() {
				if effect := p.find(id); effect != nil {
					effect.Favorite = !favorite
				}
			}, err
		}
		return nil, nil
	})
}

func (p *effectsPage) play(a *App, id string) {
	p.changes++
	p.commands++
	p.markActive(id)
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		if err := client.Play(ctx, id); err != nil {
			return func() {
				p.commands--
				if p.commands == 0 {
					p.markActive(a.state.Effect)
				}
			}, err
		}
		state, err := client.State(ctx)
		return func() {
			p.commands--
			switch {
			case p.commands > 0:
			case err != nil:
				// It is playing, and only the reading back failed: the next look tells the rest.
				a.rereadState()
			default:
				a.takeState(state)
			}
		}, nil
	})
}

func (p *effectsPage) remove(a *App, id string) {
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		if err := client.DeleteEffect(ctx, id); err != nil {
			return nil, err
		}
		return func() { p.load(a) }, nil
	})
}

func (p *effectsPage) nowCard(a *App) layout.Widget {
	return func(gtx C) D {
		if p.power.Clicked(gtx) {
			p.togglePower(a)
		}
		if p.brightness.Update(gtx) {
			p.change(a, "brightness", float64(SliderToPercent(p.brightness.Value)))
		} else if !p.brightness.Dragging() {
			p.brightness.Value = PercentToSlider(a.state.Brightness)
		}
		if p.speed.Update(gtx) {
			p.change(a, "speed", SliderToSpeed(p.speed.Value))
		} else if !p.speed.Dragging() {
			p.speed.Value = SpeedToSlider(a.state.Speed)
		}
		mode, powerWords := "Off", "Turn on"
		if a.state.Power {
			mode, powerWords = "On", "Turn off"
		}
		return a.card(gtx, "Now",
			a.fact("Effects", fmt.Sprintf("%d, %d in favourites", len(p.list), p.favorites())),
			a.fact("Mode", mode),
			a.fact("Effect", a.state.EffectName),
			a.slider("Brightness", &p.brightness, fmt.Sprintf("%d%%", a.state.Brightness)),
			a.slider("Speed", &p.speed, SpeedText(a.state.Speed)),
			func(gtx C) D { return a.labelButton(gtx, &p.power, buttonMain, "power", powerWords, "", true) },
		)
	}
}

// A slider with its name before it and its value after.
func (a *App) slider(name string, value *widget.Float, words string) layout.Widget {
	return func(gtx C) D {
		return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
			layout.Rigid(func(gtx C) D {
				gtx.Constraints.Min.X = gtx.Dp(unit.Dp(130))
				gtx.Constraints.Max.X = gtx.Constraints.Min.X
				return a.label(sizeSmall, colorMuted, name).Layout(gtx)
			}),
			layout.Flexed(1, material.Slider(a.theme, value).Layout),
			layout.Rigid(func(gtx C) D {
				gtx.Constraints.Min.X = gtx.Dp(unit.Dp(56))
				return layout.E.Layout(gtx, a.label(sizeSmall, colorText, words).Layout)
			}),
		)
	}
}

// One request out, and the next carries the latest, however fast the slider moves.
func (p *effectsPage) change(a *App, field string, value float64) {
	p.changes++
	if field == "brightness" {
		a.state.Brightness = int(value)
	} else {
		a.state.Speed = value
	}
	p.wanted[field] = value
	p.sendWanted(a)
}

func (p *effectsPage) sendWanted(a *App) {
	if p.sending || len(p.wanted) == 0 {
		return
	}
	brightness, hasBrightness := p.wanted["brightness"]
	speed, hasSpeed := p.wanted["speed"]
	p.wanted = map[string]float64{}
	p.sending = true
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		var state api.State
		var err error
		if hasBrightness {
			state, err = client.SetBrightness(ctx, int(brightness))
		}
		if err == nil && hasSpeed {
			state, err = client.SetSpeed(ctx, speed)
		}
		return func() {
			p.sending = false
			if err == nil && len(p.wanted) == 0 {
				a.takeState(state)
			}
			p.sendWanted(a)
			// The slider shows what was asked, which did not take: what holds is asked for once the
			// changes are through, as the stream sends nothing for what did not change.
			if err != nil && !p.sending {
				a.rereadState()
			}
		}, err
	})
}

func (p *effectsPage) togglePower(a *App) {
	on := !a.state.Power
	p.changes++
	p.commands++
	a.state.Power = on
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		state, err := client.SetPower(ctx, on)
		if err != nil {
			return func() {
				p.commands--
				a.rereadState()
			}, err
		}
		return func() {
			p.commands--
			if p.commands == 0 {
				a.takeState(state)
			}
		}, nil
	})
}
