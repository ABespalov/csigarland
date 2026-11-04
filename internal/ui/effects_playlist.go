// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Effects page's playlist: effects played in turn, each for its time, one fading into the next.
// Contributors: None

package ui

import (
	"context"
	"fmt"
	"slices"

	"gioui.org/layout"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
)

const (
	playlistDwellMin = 10
	playlistDwellMax = 3600
	crossFadeMax     = 5000
)

// playlistCard is what the page holds of the playlist: it is edited here, and sent whole by Save.
type playlistCard struct {
	read      bool // A garland of 1.0.0 has none, and the card is then left out.
	loading   bool // One reading at a time, so two answers never land on one edit.
	sent      int  // Counts what was sent: a reading asked before is older than its answer.
	list      api.Playlist
	saved     api.Playlist // As the garland last said it: list differs from it while edited.
	removes   []widget.Clickable
	add       widget.Clickable
	save      widget.Clickable
	play      widget.Clickable
	dwell     numberField
	crossFade numberField
	sending   bool
}

func (c *playlistCard) take(list api.Playlist) {
	c.read = true
	c.list = list
	c.list.Effects = slices.Clone(list.Effects)
	c.saved = list
	c.removes = make([]widget.Clickable, len(list.Effects))
}

// canPlay says whether Play or Stop can be pressed: Stop while the garland plays the playlist, whatever the
// editor holds, and Play only for a list saved with effects, as the garland plays what it holds.
func (c *playlistCard) canPlay(playing bool) bool {
	return !c.sending && (playing || len(c.saved.Effects) > 0)
}

// edited says the editor holds what the garland was not told yet.
func (c *playlistCard) edited() bool {
	return c.read && (!slices.Equal(c.list.Effects, c.saved.Effects) || c.list.Dwell != c.saved.Dwell || c.list.CrossFade != c.saved.CrossFade)
}

// loadPlaylist asks apart from the page's own load, as a garland of 1.0.0 has no such route. Only an
// editor that holds no edit takes the answer, so a Refresh shows a list changed elsewhere and never
// throws away the owner's.
func (p *effectsPage) loadPlaylist(a *App) {
	c := &p.playlist
	if c.loading || c.edited() {
		return
	}
	c.loading = true
	sent := c.sent
	a.background(func(ctx context.Context, client *api.Client) (func(), error) {
		list, err := client.Playlist(ctx)
		return func() {
			c.loading = false
			if err == nil && !c.edited() && !c.sending && sent == c.sent {
				c.take(list)
			}
		}, nil
	})
}

func (p *effectsPage) sendPlaylist(a *App, send func(ctx context.Context, client *api.Client) (api.Playlist, error), done string, keepDraft bool) {
	c := &p.playlist
	if c.sending {
		return
	}
	c.sending = true
	c.sent++
	a.work(func(ctx context.Context, client *api.Client) (func(), error) {
		list, err := send(ctx, client)
		if api.IsConflict(err) && !keepDraft {
			// Saved elsewhere since this editor read it: the list as it is now takes the draft's place,
			// as the schedule's does, and the owner sees what was done there before saving over it.
			if now, readErr := client.Playlist(ctx); readErr == nil {
				return func() {
					c.sending = false
					c.take(now)
				}, fmt.Errorf("the playlist was changed elsewhere, and is shown as it is now: %w", err)
			}
		}
		return func() {
			c.sending = false
			if err == nil {
				// Play and stop answer with the list as saved, which is not the editor's if it holds
				// what is not saved yet: only a save's answer replaces it.
				if keepDraft {
					c.read = true
				} else {
					c.take(list)
				}
				a.report(done)
			}
		}, err
	})
}

func (p *effectsPage) playlistWidget(a *App) layout.Widget {
	c := &p.playlist
	return func(gtx C) D {
		// Out of reach while a list is sent: its answer replaces the editor's.
		if c.sending {
			gtx = gtx.Disabled()
		}
		names := map[string]string{}
		for _, effect := range p.list {
			names[effect.ID] = effect.Name
		}
		for i := range c.removes {
			if c.removes[i].Clicked(gtx) && i < len(c.list.Effects) {
				c.list.Effects = slices.Delete(slices.Clone(c.list.Effects), i, i+1)
				c.removes = make([]widget.Clickable, len(c.list.Effects))
				break
			}
		}
		playing := a.state.Effect
		canAdd := playing != "" && !slices.Contains(c.list.Effects, playing) && len(c.list.Effects) < c.list.Max
		if c.add.Clicked(gtx) && canAdd {
			c.list.Effects = append(slices.Clone(c.list.Effects), playing)
			c.removes = make([]widget.Clickable, len(c.list.Effects))
		}
		if c.save.Clicked(gtx) {
			effects, dwell, fade, revision := slices.Clone(c.list.Effects), c.list.Dwell, c.list.CrossFade, c.saved.Revision
			p.sendPlaylist(a, func(ctx context.Context, client *api.Client) (api.Playlist, error) {
				return client.SavePlaylist(ctx, effects, dwell, fade, revision)
			}, "Playlist saved.", false)
		}
		if c.play.Clicked(gtx) && c.canPlay(a.state.Playlist) {
			start := !a.state.Playlist
			p.sendPlaylist(a, func(ctx context.Context, client *api.Client) (api.Playlist, error) {
				return client.PlayPlaylist(ctx, start)
			}, map[bool]string{true: "The playlist plays.", false: "The playlist stopped."}[start], true)
			p.load(a)
		}
		rows := []layout.Widget{
			a.hint("Plays these effects in turn, each for its time, one giving way to the next. Choosing a single effect, here or by a button, stops it. The fade between effects holds for every change of effect."),
		}
		for i, id := range c.list.Effects {
			name := names[id]
			if name == "" {
				// Kept by its id, and played once the garland has it again.
				name = id + " (not on this garland)"
			}
			remove := &c.removes[i]
			rows = append(rows, func(gtx C) D {
				return layout.Flex{Alignment: layout.Middle}.Layout(gtx,
					layout.Flexed(1, a.body(fmt.Sprintf("%d. %s", i+1, name))),
					layout.Rigid(func(gtx C) D {
						return a.iconButton(gtx, remove, buttonLosing, "trash", "Remove "+name+" from the playlist", true)
					}),
				)
			})
		}
		if len(c.list.Effects) == 0 {
			rows = append(rows, a.hint("Empty. Play an effect, then add it here."))
		}
		rows = append(rows,
			func(gtx C) D {
				return a.labelButton(gtx, &c.add, buttonOther, "add", "Add the effect playing", "", canAdd)
			},
			a.namedStepper("Each effect plays, seconds", &c.dwell, c.list.Dwell, playlistDwellMin, playlistDwellMax, func(value int) { c.list.Dwell = value }),
			a.namedStepper("Fade between effects, ms", &c.crossFade, c.list.CrossFade, 0, crossFadeMax, func(value int) { c.list.CrossFade = value }),
			func(gtx C) D {
				words, glyph := "Play the playlist", "play"
				if a.state.Playlist {
					words, glyph = "Stop the playlist", "close"
				}
				return row(gtx,
					func(gtx C) D { return a.labelButton(gtx, &c.save, buttonMain, "save", "Save playlist", "", !c.sending) },
					func(gtx C) D {
						return a.labelButton(gtx, &c.play, buttonOther, glyph, words, "", c.canPlay(a.state.Playlist))
					},
				)
			},
		)
		return a.card(gtx, fmt.Sprintf("Playlist (%d of %d)", len(c.list.Effects), c.list.Max), rows...)
	}
}
