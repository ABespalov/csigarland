// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the push updates: the stream read, and a garland that sends none.
// Contributors: None

package api

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"testing"
	"time"
)

func TestPushedStatesAreReadAsTheyCome(t *testing.T) {
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/v1/events" || r.Header.Get("Authorization") != bearerScheme+testToken {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "text/event-stream")
		fmt.Fprint(w, "event: state\ndata: {\"power\":true,\"effect\":\"aurora\",\"playlist\":true,\"playlist_at\":2,\"playlist_size\":5}\n\n")
		fmt.Fprint(w, ": keep\n\nevent: newer\ndata: {}\n\n")
		fmt.Fprint(w, "event: state\ndata: {\"power\":false}\n\n")
	})
	var states []State
	err := client.Events(context.Background(), func(state State) { states = append(states, state) })
	if !errors.Is(err, io.EOF) || len(states) != 2 {
		t.Fatalf("%v, %+v", err, states)
	}
	if !states[0].Power || states[0].PlaylistAt != 2 || states[0].PlaylistSize != 5 || states[1].Power {
		t.Fatalf("%+v", states)
	}
}

func TestAGarlandWithNoStreamSaysSo(t *testing.T) {
	for _, status := range []int{http.StatusNotFound, http.StatusServiceUnavailable} {
		_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
			http.Error(w, "no", status)
		})
		if err := client.Events(context.Background(), func(State) {}); !errors.Is(err, ErrNoEvents) {
			t.Errorf("%d: %v", status, err)
		}
	}
	if err := readEvents(strings.NewReader("event: state\ndata: {not json}\n\n"), func(State) {}, func() {}); err == nil {
		t.Error("a broken state was taken")
	}
	many := strings.Repeat("data: "+strings.Repeat("x", 1000)+"\n", 9)
	if err := readEvents(strings.NewReader(many), func(State) {}, func() {}); err == nil || errors.Is(err, io.EOF) {
		t.Errorf("an event of many lines past the bound: %v", err)
	}
	if err := readEvents(strings.NewReader(strings.Repeat("x", maxEventLine+1)), func(State) {}, func() {}); err == nil || errors.Is(err, io.EOF) {
		t.Errorf("a line past the bound: %v", err)
	}
}

// A stream that sends its first state and then nothing, not even its comments, is ended as silent, so
// the window asks for the state again rather than wait on a connection that carries nothing.
func TestASilentStreamIsEnded(t *testing.T) {
	saved := streamSilence
	streamSilence = 200 * time.Millisecond
	t.Cleanup(func() { streamSilence = saved })
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/event-stream")
		fmt.Fprint(w, "event: state\ndata: {\"power\":true}\n\n")
		w.(http.Flusher).Flush()
		<-r.Context().Done()
	})
	heard := 0
	started := time.Now()
	err := client.Events(context.Background(), func(State) { heard++ })
	if !errors.Is(err, ErrStreamSilent) || heard != 1 {
		t.Fatalf("%v after %d states", err, heard)
	}
	if took := time.Since(started); took > 5*time.Second {
		t.Fatalf("took %v to notice", took)
	}
}
