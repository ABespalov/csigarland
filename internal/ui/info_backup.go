// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Info page's backup: the garland's choices saved to a file, and put back from one.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"time"

	"gioui.org/layout"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/backup"
)

// A restore takes a request for each upload and favourite, more than one request's limit allows.
const restoreLimit = 2 * time.Minute

type backupCard struct {
	save    widget.Clickable
	restore confirmClick
	busy    bool
}

const backupWords = "Keeps the schedule, the favourites, the playlist, the screen, the power settings and the uploaded effects in one file, to put back on this garland after it was erased, or on another. Pairings, the Wi-Fi password and the HTTPS root are not in it, and are set again by hand."

func (p *infoPage) backupWidget(a *App) layout.Widget {
	c := &p.backup
	return func(gtx C) D {
		if c.save.Clicked(gtx) && !c.busy {
			p.saveBackup(a)
		}
		if c.restore.confirmed(gtx) && !c.busy {
			p.restoreBackup(a)
		}
		return a.card(gtx, "Backup", a.hint(backupWords), func(gtx C) D {
			return row(gtx,
				func(gtx C) D {
					return a.labelButton(gtx, &c.save, buttonOther, "save", "Save a backup", "", !c.busy && !a.choosing)
				},
				func(gtx C) D {
					return a.confirmButton(gtx, &c.restore, buttonOther, "upload", "Restore from a backup", !c.busy && !a.choosing)
				},
			)
		})
	}
}

// saveBackup reads the garland, then asks where to keep it, so that the file holds what was read.
func (p *infoPage) saveBackup(a *App) {
	c := &p.backup
	c.busy = true
	a.progress("Reading the garland for a backup...")
	name := strings.ReplaceAll(a.current.Address, ":", "-")
	a.aside(func(ctx context.Context, client *api.Client) (func(), error) {
		made, err := backup.Collect(ctx, client, time.Now())
		var data []byte
		if err == nil {
			data, err = backup.Encode(made)
		}
		if err != nil {
			return func() { c.busy = false }, err
		}
		return func() {
			c.busy = false
			p.writeBackup(a, data, fmt.Sprintf("csigarland-%s-%s.json", name, time.Now().Format("2006-01-02")))
		}, nil
	})
}

func (p *infoPage) writeBackup(a *App, data []byte, startName string) {
	if a.choosing {
		return
	}
	a.choosing = true
	owner := a.owner.Load()
	go func() {
		path, err := chooseSaveFile(a.lifetime, owner, "Save the backup", "*.json", "Backups (*.json)", "json", startName)
		if err == nil && path != "" {
			err = writeWhole(path, data)
		}
		a.post(func() {
			a.choosing = false
			switch {
			case err != nil:
				a.reportError(err)
			case path != "":
				a.report("Backup saved to " + path)
			default:
				a.report("")
			}
		})
	}()
}

// restoreBackup asks for the file, reads it whole, and only then sends anything, to the garland the
// owner confirmed it for: one left meanwhile, while a slow file was read, is sent nothing, nor is another.
func (p *infoPage) restoreBackup(a *App) {
	if a.choosing {
		return
	}
	a.choosing = true
	owner := a.owner.Load()
	c := &p.backup
	garland, connection := a.client, a.connection
	go func() {
		path, err := chooseFile(a.lifetime, owner, "Restore from a backup", "*.json", "Backups (*.json)")
		var read backup.Backup
		if err == nil && path != "" {
			var data []byte
			if data, err = readBounded(path, backup.MaxBytes); err == nil {
				read, err = backup.Parse(data)
			}
		}
		a.post(func() {
			a.choosing = false
			if err != nil {
				a.reportError(err)
				return
			}
			if path == "" {
				return
			}
			if garland == nil || garland != a.client || connection.Err() != nil {
				a.reportError(errors.New("the garland was left while the backup was read: nothing was restored"))
				return
			}
			c.busy = true
			a.progress("Restoring from " + path + "...")
			// The playlist's editor as it stands before anything is sent: only that draft is one the restore can
			// make out of date. One edited since, as of the list restored, read meanwhile, is newer, and stays.
			before := a.effects.playlist.draft()
			// In the window's own queue: a change made here meanwhile goes after it, not between its parts.
			a.work(func(_ context.Context, client *api.Client) (func(), error) {
				if client != garland {
					return func() { c.busy = false }, errors.New("the garland was left before the backup was sent: nothing was restored")
				}
				// Its own limit, past a single request's, and it ends as the connection does.
				ctx, cancel := context.WithTimeout(connection, restoreLimit)
				defer cancel()
				restored, err := backup.Restore(ctx, client, read, func(step string) { a.post(func() { a.progress(step) }) })
				return func() {
					c.busy = false
					p.load(a)
					// The power settings it may have put back: Tune and the Device page read them again.
					a.power = garlandPower{}
					a.device.load(a)
					// A playlist put back replaced what was there: a draft of the old one is dropped, and the card reads the
					// list again, as an edited card would not. One the backup lacks, or the garland refused, left the list as
					// it was, and the draft stands; one that may have gone through keeps it too, and a save of it is held
					// to the revision it was read with, so the garland says if the list changed.
					if restored.Playlist == backup.PlaylistReplaced && a.effects.playlist.holds(before) {
						a.effects.playlist.dropEdits()
						a.effects.loadPlaylist(a)
					}
					if err == nil {
						a.report(fmt.Sprintf("Restored the backup made %s from firmware %s.", read.Made, read.Firmware))
					}
				}, err
			})
		})
	}()
}

// writeWhole writes data through a file beside path, then puts it in path's place: a write that fails
// or is cut short leaves the file there as it was, an earlier backup or effect file among them. The file beside it is
// this write's own, so two saves to one path at once each put only their own bytes there.
func writeWhole(path string, data []byte) error {
	file, err := os.CreateTemp(filepath.Dir(path), filepath.Base(path)+".*.new")
	if err != nil {
		return fmt.Errorf("write %s: %w", filepath.Base(path), err)
	}
	partial := file.Name()
	_, err = file.Write(data)
	if closeErr := file.Close(); err == nil {
		err = closeErr
	}
	if err != nil {
		_ = os.Remove(partial)
		return fmt.Errorf("write %s: %w", filepath.Base(path), err)
	}
	if err := os.Rename(partial, path); err != nil {
		_ = os.Remove(partial)
		return fmt.Errorf("write %s: %w", filepath.Base(path), err)
	}
	return nil
}

// readBounded reads at most limit bytes of a file, and fails on a larger one: what it reads is what was
// open, never more, though the file grow or be replaced meanwhile.
func readBounded(path string, limit int64) ([]byte, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", filepath.Base(path), err)
	}
	defer file.Close()
	data, err := io.ReadAll(io.LimitReader(file, limit+1))
	if err != nil {
		return nil, fmt.Errorf("read %s: %w", filepath.Base(path), err)
	}
	if int64(len(data)) > limit {
		return nil, fmt.Errorf("%s is over %d bytes, which no file of this kind is", filepath.Base(path), limit)
	}
	return data, nil
}
