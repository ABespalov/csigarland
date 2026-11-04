// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The routes of the playlist and of the schedule's next 24 hours.
// Contributors: None

package api

import (
	"context"
	"fmt"
)

// Playlist is the list of effects the garland plays in turn, and how.
type Playlist struct {
	Effects []string `json:"effects"`
	// Missing names those of Effects the garland's catalog lacks, which it keeps and plays once they are
	// there, new after 1.1.0's first builds.
	Missing   []string `json:"missing,omitempty"`
	Dwell     int      `json:"dwell"`      // Seconds each effect plays, 10 to 3600.
	CrossFade int      `json:"cross_fade"` // Milliseconds of fade from one effect to the next, 0 to 5000.
	Playing   bool     `json:"playing"`
	Max       int      `json:"max"`
	// Moves with every save, from 1 at each start; 0 from a garland that does not say, as before 1.1.0.
	Revision uint32 `json:"revision,omitempty"`
}

// Playlist reads the playlist.
func (c *Client) Playlist(ctx context.Context) (Playlist, error) {
	var playlist Playlist
	return playlist, c.get(ctx, "playlist", nil, &playlist)
}

// SetPlaylist replaces the list, how long each effect plays and the fade between them, whatever it is
// now, as a restore means to. An empty list stops the playlist.
func (c *Client) SetPlaylist(ctx context.Context, effects []string, dwell, crossFade int) (Playlist, error) {
	return c.SavePlaylist(ctx, effects, dwell, crossFade, 0)
}

// SavePlaylist replaces the list as SetPlaylist does, but only if it is still of revision, the one it was
// read with: one saved since by another page or program is a conflict, see IsConflict. Revision 0 replaces
// whatever is there, as a garland that gives no revision needs.
func (c *Client) SavePlaylist(ctx context.Context, effects []string, dwell, crossFade int, revision uint32) (Playlist, error) {
	if effects == nil {
		effects = []string{}
	}
	var playlist Playlist
	body := map[string]any{"effects": effects, "dwell": dwell, "cross_fade": crossFade}
	if revision != 0 {
		body["revision"] = revision
	}
	return playlist, c.post(ctx, "playlist", body, &playlist)
}

// PlayPlaylist starts the playlist, which turns the garland on at its first effect, or stops it and
// keeps what plays. Starting an empty one is a conflict: see IsConflict.
func (c *Client) PlayPlaylist(ctx context.Context, play bool) (Playlist, error) {
	var playlist Playlist
	return playlist, c.post(ctx, "playlist/play", map[string]bool{"play": play}, &playlist)
}

// TimelineEvent is one minute of the next 24 hours with actions due, and what they add up to.
type TimelineEvent struct {
	At         int64   `json:"at"`      // Unix seconds.
	Time       string  `json:"time"`    // "18:00", in the garland's time zone.
	Day        int     `json:"day"`     // 0 is Sunday.
	Entries    []int   `json:"entries"` // Positions in the schedule's list: more than one meet here.
	Power      string  `json:"power,omitempty"`
	Brightness int     `json:"brightness,omitempty"`
	Speed      float64 `json:"speed,omitempty"`
	Effect     string  `json:"effect,omitempty"`
	Playlist   bool    `json:"playlist,omitempty"`
}

// Timeline is the schedule's next 24 hours, as the garland works it out.
type Timeline struct {
	Synced bool            `json:"synced"` // False with the clock not set, when nothing can be said.
	Now    int64           `json:"now"`    // The garland's time when it answered, Unix seconds, to draw the day from.
	Events []TimelineEvent `json:"events"`
	// Power is whether the garland was lit as the day was worked out, one snapshot with it, from builds of
	// 1.1.0 on; nil from one that does not say.
	Power *bool `json:"power,omitempty"`
}

// Timeline reads the next 24 hours of the schedule.
func (c *Client) Timeline(ctx context.Context) (Timeline, error) {
	var timeline Timeline
	if err := c.get(ctx, "schedule/timeline", nil, &timeline); err != nil {
		return Timeline{}, err
	}
	// A day of the week out of its range is no answer of a garland's: refused whole, never drawn.
	for _, event := range timeline.Events {
		if event.Day < 0 || event.Day > 6 {
			return Timeline{}, fmt.Errorf("the garland's day gives the weekday %d, which no day is", event.Day)
		}
	}
	return timeline, nil
}
