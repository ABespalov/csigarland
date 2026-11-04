// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Push updates in the window: the garland's state as it sends it, in place of asking for it.
// Contributors: None

package ui

import (
	"errors"
	"time"

	"github.com/ABespalov/csigarland/internal/api"
)

const (
	// After a stream that ended, as a restart or an update ends it.
	listenAgain = 10 * time.Second
	// After a garland that sends none: one of 1.0.0, or with two streams open elsewhere.
	listenLater = time.Minute
)

// listen keeps a stream of the garland's state open for as long as this connection lasts, and opens
// it again a while after it ends. While it is open the window asks for no state: see refreshState.
func (a *App) listen() {
	client, connection := a.client, a.connection
	if client == nil {
		return
	}
	go func() {
		for connection.Err() == nil {
			err := client.Events(connection, func(state api.State) {
				a.post(func() { a.takePushed(client, state) })
			})
			a.post(func() {
				if client == a.client {
					a.pushing = false
				}
			})
			wait := listenAgain
			if errors.Is(err, api.ErrNoEvents) {
				wait = listenLater
			}
			select {
			case <-connection.Done():
				return
			case <-time.After(wait):
			}
		}
	}()
}

// rereadState asks for the state though a stream is open: a change of the window's own, on its way,
// passed over what the stream sent meanwhile, which it sends only once.
func (a *App) rereadState() {
	a.mustRead = true
	a.refreshState()
}

// takePushed takes a state the garland sent, unless it is from a garland since left, or a change of
// the owner's is on its way. That change's answer may have been made before this state, as by a button
// on the garland meanwhile, so the state is asked for again once the changes are through.
func (a *App) takePushed(client *api.Client, state api.State) {
	if client != a.client {
		return
	}
	if !a.pushing {
		// A stream opened again may follow a restart, as by an update sent from elsewhere: what the
		// garland says of itself comes only with About, asked for until it comes.
		a.pushing = true
		a.aboutStale = true
		a.refreshAbout()
	}
	if a.effects.sending || a.effects.commands > 0 {
		a.mustRead = true
		return
	}
	// Newer than anything asked for before it, and than what was passed over.
	a.pushes++
	a.mustRead = false
	a.takeState(state)
}
