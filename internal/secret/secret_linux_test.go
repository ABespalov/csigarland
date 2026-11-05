// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Holds the Linux token store to what a system with no Secret Service gets.
// Contributors: None

package secret

import (
	"errors"
	"path/filepath"
	"testing"
)

// With no session bus, a token is kept nowhere less safe, none is found, and forgetting is no failure.
func TestNoSessionBusIsNoStore(t *testing.T) {
	t.Setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path="+filepath.Join(t.TempDir(), "no-bus"))
	if err := Set("any", "abc"); !errors.Is(err, ErrUnsupported) {
		t.Fatalf("set: %v", err)
	}
	if _, err := Get("any"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("get: %v", err)
	}
	if err := Delete("any"); err != nil {
		t.Fatalf("delete: %v", err)
	}
}

func TestAnItemIsFoundByItsTarget(t *testing.T) {
	got := attributes("ab12")
	if got[attributeApp] != "csiGarland" || got[attributeTarget] != "csiGarland/ab12" || len(got) != 2 {
		t.Fatalf("%v", got)
	}
}

// A prompt the owner dismissed is not shown again, so the tokens asked for in turn ask the owner once.
func TestADismissedPromptIsNotShownAgain(t *testing.T) {
	dismissed.Store(true)
	t.Cleanup(func() { dismissed.Store(false) })
	if err := (&store{}).prompt("/org/freedesktop/secrets/prompt/u1"); !errors.Is(err, errDismissed) {
		t.Fatalf("a prompt after one dismissed: %v", err)
	}
	if err := (&store{}).prompt(noPath); err != nil {
		t.Fatalf("no prompt at all: %v", err)
	}
}
