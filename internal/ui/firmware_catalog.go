// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Firmware page's online catalog: released update files to choose from, downloaded and checked.
// Contributors: None

package ui

import (
	"fmt"
	"net/http"

	"gioui.org/layout"
	"gioui.org/widget"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/catalog"
)

// releaseCatalog is what the Firmware page has of the catalog: its entries once read, and a button for each.
type releaseCatalog struct {
	open     widget.Clickable
	shown    bool
	fetching bool
	address  string
	entries  []catalog.Entry
	picks    map[string]*widget.Clickable
	http     *http.Client
}

func (c *releaseCatalog) client() *http.Client {
	if c.http == nil {
		c.http = &http.Client{Timeout: catalog.Timeout}
	}
	return c.http
}

// releasesAddress is the catalog's, unless a page names another. Empty, nothing is looked for by
// itself, as in the tests.
var releasesAddress = catalog.DefaultURL

func (c *releaseCatalog) url() string {
	if c.address == "" {
		return releasesAddress
	}
	return c.address
}

// fetchCatalog reads the list, off the window's goroutine.
func (p *firmwarePage) fetchCatalog(a *App) {
	c := &p.releases
	if c.fetching {
		return
	}
	c.fetching = true
	a.progress("Reading the catalog of releases...")
	client, address, connection := c.client(), c.url(), a.connection
	go func() {
		read, err := catalog.Fetch(connection, client, address)
		a.post(func() {
			c.fetching = false
			if err != nil {
				a.reportError(err)
				return
			}
			c.entries = read.Firmware
			c.shown = true
			a.progress("")
		})
	}()
}

// download gets an entry's file, held to the catalog's length and SHA-256, and takes it as a file
// chosen. With install, it is sent to the garland once here, as Install would send it.
func (p *firmwarePage) download(a *App, entry catalog.Entry, install bool) {
	c := &p.releases
	if c.fetching {
		return
	}
	c.fetching = true
	a.progress(fmt.Sprintf("Downloading %s...", entry.File))
	client := c.client()
	garland, connection := a.client, a.connection
	// A file chosen while this comes down, or the file dialog only opened, is the owner's latest word,
	// and this one then neither replaces it nor goes to the garland.
	p.choice++
	choice := p.choice
	go func() {
		data, err := catalog.Download(connection, client, entry)
		var update api.UpdateFile
		if err == nil {
			update, err = api.ParseUpdate(data)
		}
		// The file is the catalog's by its SHA-256, which says nothing of what it holds: one whose firmware
		// is not the version the row names, signed as it may be, would install another, an older among them.
		if err == nil {
			err = catalog.HoldsVersion(entry, update.HasFirmware, update.Version)
		}
		a.post(func() {
			// For a garland since left, whose page this no longer is.
			if garland != a.client {
				return
			}
			c.fetching = false
			if choice != p.choice {
				a.report(fmt.Sprintf("Downloaded %s, and left it, as a file was being chosen here meanwhile.", entry.File))
				return
			}
			if err != nil {
				a.reportError(err)
				return
			}
			c.shown = false
			p.path = entry.File
			p.file = data
			p.update = update
			p.contents = UpdateText(update) + " Downloaded from the catalog, and its SHA-256 is the catalog's."
			a.report(fmt.Sprintf("Downloaded %s.", entry.File))
			// Sent as the offer was made: unless the garland runs this, or newer, by now, as send reads it.
			if install && a.connected && !p.busy {
				p.send(a, entry.Version)
			}
		})
	}()
}

// catalogRows are the catalog's releases, newest first, each to press for its file.
func (p *firmwarePage) catalogRows(a *App, gtx C) []layout.Widget {
	c := &p.releases
	if !c.shown {
		return nil
	}
	if c.picks == nil {
		c.picks = map[string]*widget.Clickable{}
	}
	if len(c.entries) == 0 {
		return []layout.Widget{a.hint("The catalog lists no releases yet.")}
	}
	rows := []layout.Widget{a.hint("Released update files, newest first, from " + c.url() + ". The garland checks the signature of whichever you choose.")}
	items := make([]layout.Widget, 0, len(c.entries))
	for i, entry := range c.entries {
		pick := c.picks[entry.Version]
		if pick == nil {
			pick = new(widget.Clickable)
			c.picks[entry.Version] = pick
		}
		if pick.Clicked(gtx) && !c.fetching && !p.busy {
			p.download(a, entry, false)
		}
		words := entry.Version
		if entry.Version == p.running.Version {
			words += ", running"
		} else if catalog.Newer(entry.Version, p.running.Version) {
			words += ", newer"
		}
		kind := buttonOther
		if i == 0 && catalog.Newer(entry.Version, p.running.Version) {
			kind = buttonMain
		}
		tip := fmt.Sprintf("%s, %d effects, published %s", entry.File, entry.Effects, entry.Published)
		items = append(items, func(gtx C) D { return a.labelButton(gtx, pick, kind, "upload", words, tip, !c.fetching && !p.busy) })
	}
	return append(rows, func(gtx C) D { return row(gtx, items...) })
}
