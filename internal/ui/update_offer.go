// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: After connecting, a look at the catalog of releases, and an offer to install a newer firmware.
// Contributors: None

package ui

import (
	"context"
	"fmt"

	"gioui.org/layout"
	"gioui.org/widget"
	"github.com/rs/zerolog/log"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/catalog"
)

// updateOffer is a newer release than the garland connected to runs, once per connection.
type updateOffer struct {
	looked  bool // At the catalog, for this connection.
	entry   catalog.Entry
	running string
	install widget.Clickable
	later   widget.Clickable
}

const offerWords = "It is downloaded, held to the catalog's SHA-256, and sent to the garland, which checks its signature, restarts, and confirms the new firmware after its first minute or goes back to this one. Settings, pairings and uploaded effects stay."

// lookForUpdate asks the garland what it runs and the catalog what is released, and offers the newest
// release if it is newer. A catalog out of reach, as with no internet, is no failure of the garland's,
// and says nothing.
func (a *App) lookForUpdate() {
	releases := &a.firmware.releases
	address := releases.url()
	if a.offer.looked || address == "" {
		return
	}
	a.offer.looked = true
	http := releases.client()
	a.background(func(ctx context.Context, client *api.Client) (func(), error) {
		running, err := client.Firmware(ctx)
		if err != nil {
			return nil, err
		}
		read, err := catalog.Fetch(ctx, http, address)
		if err != nil {
			log.Info().Err(err).Msg("the catalog of releases could not be read")
			return nil, nil
		}
		entry, newer := catalog.Offer(read, running.Version)
		if !newer {
			return nil, nil
		}
		return func() {
			// Not over a window the owner has open, nor while an update already goes up.
			if a.modal != modalNone || a.schedulePopupShown() || a.firmware.busy {
				return
			}
			a.offer.entry, a.offer.running = entry, running.Version
			a.openModal(modalOffer)
		}, nil
	})
}

func (o *updateOffer) body(a *App) layout.Widget {
	return func(gtx C) D {
		if o.later.Clicked(gtx) {
			a.closeModal(gtx.Now)
		}
		if o.install.Clicked(gtx) {
			a.closeModal(gtx.Now)
			a.choosePage(PageFirmware)
			a.firmware.download(a, o.entry, true)
		}
		return a.column(gtx,
			func(gtx C) D { return a.modalTitle(gtx, "A newer firmware") },
			a.body(fmt.Sprintf("Firmware %s is released, with %d built-in effects. This garland runs %s.", o.entry.Version, o.entry.Effects, o.running)),
			a.hint(offerWords),
			func(gtx C) D {
				return row(gtx,
					func(gtx C) D {
						return a.labelButton(gtx, &o.install, buttonMain, "upload", "Install "+o.entry.Version, "", true)
					},
					func(gtx C) D { return a.labelButton(gtx, &o.later, buttonOther, "close", "Not now", "", true) },
				)
			},
		)
	}
}
