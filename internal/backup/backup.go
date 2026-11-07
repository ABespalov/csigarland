// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: A garland's own choices in one file and back: the schedule, favourites, playlist, screen, power and uploads.
// Contributors: None

// Package backup keeps what an owner has set up on a garland, all of it the public API gives, in one
// file, and puts it back on a garland: the same one after it was erased, or another. Nothing secret is
// in it, as the API returns no password, token or key: pairings, the Wi-Fi password and the HTTPS
// root stay on the garland, and a garland erased needs those again by hand.
package backup

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"maps"
	"net/http"
	"slices"
	"strings"
	"time"

	"github.com/ABespalov/csigarland/internal/api"
)

const (
	// Format is the file's own format, which Parse refuses if it is another.
	Format = "csigarland.backup/1"
	// MaxBytes bounds a backup: 32 uploads of 8 KB each and everything else, with room.
	MaxBytes   = 1 << 20
	maxUploads = 64
)

// Upload is an effect the owner added from a file, as the garland keeps it.
type Upload struct {
	ID   string `json:"id"`
	File string `json:"file"` // The file's bytes as the garland keeps them, as text, so they come back the same.
}

// Backup is the file.
type Backup struct {
	Format   string       `json:"format"`
	Made     string       `json:"made"`     // RFC 3339, when it was made.
	Firmware string       `json:"firmware"` // The version it was made from.
	Schedule []api.Action `json:"schedule"`
	// The effects the schedule names that its garland lacked and kept, as the playlist's Missing says.
	ScheduleMissing []string      `json:"schedule_missing,omitempty"`
	Favorites       []string      `json:"favorites"`
	Playlist        *api.Playlist `json:"playlist,omitempty"` // Absent from a garland of 1.0.0, which has none.
	Display         api.Display   `json:"display"`
	// The LEDs' power settings, the four the owner sets. Absent from a garland of 1.1.1 or before.
	Power   *api.PowerBudget `json:"power,omitempty"`
	Uploads []Upload         `json:"uploads"`
}

// picture says which list, effects and playlist a backup was read from: read again once it is made, a
// picture that changed means another page or program changed the garland meanwhile. An upload is known
// by its file's bytes too, as one replaced under the same id changes nothing else the API says.
type picture struct {
	schedule uint32
	playlist uint32
	effects  string
}

func pictureOf(schedule api.Schedule, effects []api.Effect, files map[string][]byte, playlist *api.Playlist) picture {
	var names strings.Builder
	for _, effect := range effects {
		fmt.Fprintf(&names, "%s:%t:%t", effect.ID, effect.Uploaded, effect.Favorite)
		if effect.Uploaded {
			fmt.Fprintf(&names, ":%x", sha256.Sum256(files[effect.ID]))
		}
		names.WriteByte(';')
	}
	seen := picture{schedule: schedule.Revision, effects: names.String()}
	if playlist != nil {
		seen.playlist = playlist.Revision
	}
	return seen
}

// errChanged says the garland changed under a reading, as an upload deleted between the list of effects
// and its file: read it again, as any other change.
var errChanged = errors.New("the garland changed while it was read")

// Collect reads all of it from a garland, as one picture: one that changed while it was read, by another
// page or program, is read again, up to three times. Every effect its schedule and playlist name must be
// built into the garland or in the backup, or the backup would put back what it cannot play, but for one
// the playlist keeps that the garland itself lacks, which the backup keeps by its id, as the garland does.
func Collect(ctx context.Context, client *api.Client, now time.Time) (Backup, error) {
	for range 3 {
		made, seen, builtIn, err := collectOnce(ctx, client, now)
		if errors.Is(err, errChanged) {
			continue
		}
		if err != nil {
			return Backup{}, err
		}
		after, err := readPicture(ctx, client, made.Playlist != nil)
		if errors.Is(err, errChanged) {
			continue
		}
		if err != nil {
			return Backup{}, err
		}
		if after == seen {
			return made, namesAreHeld(made, builtIn)
		}
	}
	return Backup{}, errors.New("the garland changed while it was read, three times over: try again once it is left alone")
}

// effectFile reads an upload the list of effects just named: one gone since, 404, is a change of the
// garland, not a failure.
func effectFile(ctx context.Context, client *api.Client, id string) ([]byte, error) {
	file, err := client.EffectFile(ctx, id)
	var failure *api.Error
	if errors.As(err, &failure) && failure.Status == http.StatusNotFound {
		return nil, fmt.Errorf("the effect %s: %w", id, errChanged)
	}
	return file, err
}

func readPicture(ctx context.Context, client *api.Client, withPlaylist bool) (picture, error) {
	schedule, err := client.Schedule(ctx)
	if err != nil {
		return picture{}, fmt.Errorf("read the schedule: %w", err)
	}
	effects, err := client.Effects(ctx)
	if err != nil {
		return picture{}, fmt.Errorf("read the effects: %w", err)
	}
	files := map[string][]byte{}
	for _, effect := range effects {
		if !effect.Uploaded {
			continue
		}
		if files[effect.ID], err = effectFile(ctx, client, effect.ID); err != nil {
			return picture{}, fmt.Errorf("read the effect %s: %w", effect.ID, err)
		}
	}
	if !withPlaylist {
		return pictureOf(schedule, effects, files, nil), nil
	}
	playlist, err := client.Playlist(ctx)
	if err != nil {
		return picture{}, fmt.Errorf("read the playlist: %w", err)
	}
	return pictureOf(schedule, effects, files, &playlist), nil
}

// namesAreHeld says which effect the schedule or the playlist names that the backup could not put back.
func namesAreHeld(made Backup, builtIn map[string]bool) error {
	held := maps.Clone(builtIn)
	for _, upload := range made.Uploads {
		held[upload.ID] = true
	}
	for _, action := range made.Schedule {
		if action.Effect != "" && !held[action.Effect] && !slices.Contains(made.ScheduleMissing, action.Effect) {
			return fmt.Errorf("the schedule names the effect %s, which the garland lacks: the backup would not hold it", action.Effect)
		}
	}
	if made.Playlist != nil {
		for _, id := range made.Playlist.Effects {
			if !held[id] && !slices.Contains(made.Playlist.Missing, id) {
				return fmt.Errorf("the playlist names the effect %s, which the garland lacks: the backup would not hold it", id)
			}
		}
	}
	return nil
}

func collectOnce(ctx context.Context, client *api.Client, now time.Time) (Backup, picture, map[string]bool, error) {
	backup := Backup{Format: Format, Made: now.UTC().Format(time.RFC3339), Schedule: []api.Action{}, Favorites: []string{}, Uploads: []Upload{}}
	builtIn := map[string]bool{}
	files := map[string][]byte{}
	firmware, err := client.Firmware(ctx)
	if err != nil {
		return Backup{}, picture{}, nil, fmt.Errorf("read the firmware: %w", err)
	}
	backup.Firmware = firmware.Version
	schedule, err := client.Schedule(ctx)
	if err != nil {
		return Backup{}, picture{}, nil, fmt.Errorf("read the schedule: %w", err)
	}
	backup.Schedule = append(backup.Schedule, schedule.Entries...)
	backup.ScheduleMissing = schedule.Missing
	effects, err := client.Effects(ctx)
	if err != nil {
		return Backup{}, picture{}, nil, fmt.Errorf("read the effects: %w", err)
	}
	for _, effect := range effects {
		if effect.BuiltIn {
			builtIn[effect.ID] = true
		}
		if effect.Favorite {
			backup.Favorites = append(backup.Favorites, effect.ID)
		}
		if !effect.Uploaded {
			continue
		}
		file, err := effectFile(ctx, client, effect.ID)
		if err != nil {
			return Backup{}, picture{}, nil, fmt.Errorf("read the effect %s: %w", effect.ID, err)
		}
		if !json.Valid(file) {
			return Backup{}, picture{}, nil, fmt.Errorf("the effect %s is not JSON as the garland gave it", effect.ID)
		}
		files[effect.ID] = file
		backup.Uploads = append(backup.Uploads, Upload{ID: effect.ID, File: string(file)})
	}
	// A garland of 1.0.0 has no playlist, and says so with 404; one of a release with it that answers 404
	// fails, as Check the API says of it. Anything else that fails is no reason to leave it out of a backup
	// called made.
	var failure *api.Error
	switch playlist, err := client.Playlist(ctx); {
	case err == nil:
		backup.Playlist = &playlist
	case errors.As(err, &failure) && failure.Status == http.StatusNotFound && !api.HasRoute(firmware.Version, "playlist"):
	default:
		return Backup{}, picture{}, nil, fmt.Errorf("read the playlist: %w", err)
	}
	if backup.Display, err = client.Display(ctx); err != nil {
		return Backup{}, picture{}, nil, fmt.Errorf("read the screen: %w", err)
	}
	// As the playlist: 404 only from a release before the power settings.
	switch budget, err := client.PowerBudget(ctx); {
	case err == nil:
		backup.Power = &api.PowerBudget{SupplyMilliamps: budget.SupplyMilliamps, WhiteMilliamps: budget.WhiteMilliamps, IdleMilliamps: budget.IdleMilliamps, MarginPercent: budget.MarginPercent}
	case errors.As(err, &failure) && failure.Status == http.StatusNotFound && !api.HasRoute(firmware.Version, "leds/power"):
	default:
		return Backup{}, picture{}, nil, fmt.Errorf("read the power settings: %w", err)
	}
	return backup, pictureOf(schedule, effects, files, backup.Playlist), builtIn, nil
}

// Encode is the file's bytes.
func Encode(backup Backup) ([]byte, error) {
	data, err := json.MarshalIndent(backup, "", "  ")
	if err != nil {
		return nil, fmt.Errorf("write the backup: %w", err)
	}
	return append(data, '\n'), nil
}

// ErrFormat says a file is not a backup this utility reads.
var ErrFormat = errors.New("the file is not a csiGarland backup this utility reads")

// Parse reads a backup's bytes: one JSON value of the known format, bounded, every field known.
func Parse(data []byte) (Backup, error) {
	if len(data) > MaxBytes {
		return Backup{}, fmt.Errorf("the backup is over %d bytes", MaxBytes)
	}
	decoder := json.NewDecoder(bytes.NewReader(data))
	decoder.DisallowUnknownFields()
	var backup Backup
	if err := decoder.Decode(&backup); err != nil {
		return Backup{}, fmt.Errorf("the backup is not valid: %w", err)
	}
	// Nothing but space after it: More says no before a closing bracket too, which is no end of the file.
	var extra json.RawMessage
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		return Backup{}, errors.New("the backup is not valid: something follows it")
	}
	if backup.Format != Format {
		return Backup{}, ErrFormat
	}
	// Every part, though it be empty: a part left out would read as nothing, and replace what is there.
	switch {
	case backup.Made == "" || backup.Firmware == "":
		return Backup{}, errors.New("the backup does not say when or from what it was made")
	case backup.Schedule == nil:
		return Backup{}, errors.New("the backup holds no schedule, not even an empty one")
	case backup.Favorites == nil:
		return Backup{}, errors.New("the backup holds no favourites, not even an empty list")
	case backup.Uploads == nil:
		return Backup{}, errors.New("the backup holds no uploads, not even an empty list")
	case backup.Display.Saver == "":
		return Backup{}, errors.New("the backup holds no screen settings")
	}
	if len(backup.Uploads) > maxUploads {
		return Backup{}, fmt.Errorf("the backup holds %d uploads, more than a garland keeps", len(backup.Uploads))
	}
	for _, upload := range backup.Uploads {
		if upload.ID == "" || len(upload.File) == 0 {
			return Backup{}, errors.New("the backup holds an upload with no id or no file")
		}
	}
	return backup, nil
}

// Restore puts a backup on a garland, in the order that lets each part find what it names: the uploads
// first, then the favourites, the playlist, the screen, the power settings and last the schedule, which replaces the
// garland's whole list. Each step that fails is said, and the rest still go on, so a garland that
// lacks one effect takes all else. step hears each part as it is done. The schedule replaced is the one
// read before anything is sent: one saved meanwhile, from here or elsewhere, is not overwritten.
func Restore(ctx context.Context, client *api.Client, backup Backup, step func(string)) error {
	var problems []error
	fail := func(what string, err error) { problems = append(problems, fmt.Errorf("%s: %w", what, err)) }
	before, scheduleErr := client.Schedule(ctx)
	for _, upload := range backup.Uploads {
		if err := client.SaveEffect(ctx, upload.ID, []byte(upload.File)); err != nil {
			fail("effect "+upload.ID, err)
		}
	}
	step(fmt.Sprintf("Put back %d uploaded effects.", len(backup.Uploads)))
	effects, err := client.Effects(ctx)
	// Whether the garland's effects were read, apart from what is done with its favourites.
	catalogRead := err == nil
	if err != nil {
		fail("read the effects", err)
	}
	// Ticked before untick, so the garland is never left with none, which it refuses; and none at all if
	// no favourite of the backup is here, which would leave only unticking.
	ticked := effects
	known := slices.ContainsFunc(effects, func(effect api.Effect) bool { return slices.Contains(backup.Favorites, effect.ID) })
	if !known && len(effects) > 0 {
		fail("the favourites", errors.New("none of the backup's is on this garland, so its own stay"))
		ticked = nil
	}
	for _, ticking := range []bool{true, false} {
		for _, effect := range ticked {
			wanted := slices.Contains(backup.Favorites, effect.ID)
			if wanted == ticking && effect.Favorite != wanted {
				if err := client.SetFavorite(ctx, effect.ID, wanted); err != nil {
					fail("favourite "+effect.ID, err)
				}
			}
		}
	}
	step("Put back the favourites.")
	if backup.Playlist != nil {
		// An effect this garland lacks, as one its source kept missing, is left out, and said: the rest of
		// the list still comes back.
		here := map[string]bool{}
		for _, effect := range effects {
			here[effect.ID] = true
		}
		var kept, left []string
		for _, id := range backup.Playlist.Effects {
			if here[id] || !catalogRead {
				kept = append(kept, id)
			} else {
				left = append(left, id)
			}
		}
		if _, err := client.SetPlaylist(ctx, kept, backup.Playlist.Dwell, backup.Playlist.CrossFade); err != nil {
			fail("the playlist", err)
		}
		if len(left) > 0 {
			fail("the playlist", fmt.Errorf("left out %s, which this garland lacks", strings.Join(left, ", ")))
		}
		step("Put back the playlist.")
	}
	if backup.Display.Saver != "" {
		if _, err := client.SetDisplay(ctx, backup.Display); err != nil {
			fail("the screen", err)
		}
		step("Put back the screen.")
	}
	if backup.Power != nil {
		if _, err := client.SetPowerBudget(ctx, *backup.Power); err != nil {
			fail("the power settings", err)
		}
		step("Put back the power settings.")
	}
	// An action naming an effect this garland lacks is left out, and said, as the garland would refuse the
	// whole list for it: unless this garland keeps that effect missing in its own list already, as the one
	// the backup was made from does.
	actions, left := backup.Schedule, []string{}
	if catalogRead && scheduleErr == nil {
		here := map[string]bool{}
		for _, effect := range effects {
			here[effect.ID] = true
		}
		actions = nil
		for _, action := range backup.Schedule {
			if action.Effect == "" || here[action.Effect] || slices.Contains(before.Missing, action.Effect) {
				actions = append(actions, action)
			} else if !slices.Contains(left, action.Effect) {
				left = append(left, action.Effect)
			}
		}
	}
	err = scheduleErr
	if err == nil {
		_, err = client.SetSchedule(ctx, actions, before.Revision)
	}
	if err != nil {
		fail("the schedule", err)
	} else {
		step(fmt.Sprintf("Put back %d scheduled actions.", len(actions)))
	}
	if len(left) > 0 {
		fail("the schedule", fmt.Errorf("left out the actions naming %s, which this garland lacks", strings.Join(left, ", ")))
	}
	return errors.Join(problems...)
}
