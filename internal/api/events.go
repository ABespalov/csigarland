// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Push updates: the garland's state as it sends it, over a stream of Server-Sent Events.
// Contributors: None

package api

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"sync/atomic"
	"time"
)

// A line of the stream, and an event of however many lines, past which it is not one the garland sends.
const (
	maxEventLine = 4 << 10
	maxEventData = 8 << 10
)

// ErrNoEvents says the garland gives no stream now: one of 1.0.0, two open elsewhere, or an update
// under way. Asking now and then still works.
var ErrNoEvents = errors.New("the garland sends no push updates now")

// ErrStreamSilent says a stream sent nothing, not even its comment every 15 s, for streamSilence: a
// connection that stopped carrying data without ending, which the window then asks around.
var ErrStreamSilent = errors.New("the garland's stream fell silent")

// Three of the garland's 15 s comments missed. A variable, for the tests.
var streamSilence = 45 * time.Second

// Events reads the garland's stream until ctx ends or the stream does, and hands each state it sends
// to onState, the first at once. It has a connection of its own, with no time limit as a whole, so the
// requests of this client never wait behind it. It returns ErrNoEvents when the garland refuses one.
func (c *Client) Events(ctx context.Context, onState func(State)) error {
	transport, ok := c.http.Transport.(*http.Transport)
	if !ok {
		return errors.New("the client has no transport of its own to clone")
	}
	// Ended when it falls silent too: a stream is alive by what it sends.
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	var silent atomic.Bool
	timer := time.AfterFunc(streamSilence, func() {
		silent.Store(true)
		cancel()
	})
	defer timer.Stop()
	stream := &http.Client{Transport: transport.Clone()}
	defer stream.CloseIdleConnections()
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, c.base+prefix+"events", nil)
	if err != nil {
		return fmt.Errorf("make the request: %w", err)
	}
	request.Header.Set("Accept", "text/event-stream")
	if c.token != "" {
		request.Header.Set("Authorization", bearerScheme+c.token)
	}
	response, err := stream.Do(request)
	if err != nil {
		return fmt.Errorf("reach the garland: %w", err)
	}
	defer response.Body.Close()
	switch {
	case response.StatusCode == http.StatusNotFound || response.StatusCode == http.StatusServiceUnavailable:
		return ErrNoEvents
	case response.StatusCode >= http.StatusBadRequest:
		return readError(response)
	case !strings.HasPrefix(response.Header.Get("Content-Type"), "text/event-stream"):
		return ErrNoEvents
	}
	err = readEvents(response.Body, onState, func() { timer.Reset(streamSilence) })
	if silent.Load() {
		return ErrStreamSilent
	}
	return err
}

// readEvents takes the events of a stream, of which only state is read: a newer firmware's others are
// passed over.
func readEvents(body io.Reader, onState func(State), heard func()) error {
	scanner := bufio.NewScanner(body)
	scanner.Buffer(make([]byte, 0, 512), maxEventLine)
	name, data := "", ""
	for scanner.Scan() {
		heard()
		line := scanner.Text()
		switch {
		case line == "":
			if name == "state" && data != "" {
				var state State
				if err := json.Unmarshal([]byte(data), &state); err != nil {
					return fmt.Errorf("read a pushed state: %w", err)
				}
				onState(state)
			}
			name, data = "", ""
		case strings.HasPrefix(line, ":"):
		case strings.HasPrefix(line, "event:"):
			name = strings.TrimSpace(strings.TrimPrefix(line, "event:"))
		case strings.HasPrefix(line, "data:"):
			data += strings.TrimPrefix(strings.TrimPrefix(line, "data:"), " ")
			if len(data) > maxEventData {
				return fmt.Errorf("read the stream: an event over %d bytes", maxEventData)
			}
		}
	}
	if err := scanner.Err(); err != nil {
		return fmt.Errorf("read the stream: %w", err)
	}
	return io.EOF
}
