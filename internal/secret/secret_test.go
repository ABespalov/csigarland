// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: A test of the system's token store, with a name of its own that it removes.
// Contributors: None

package secret

import (
	"errors"
	"testing"
)

func TestATokenComesBackUntilForgotten(t *testing.T) {
	const name = "test-of-the-store-can-be-deleted"
	if err := Set(name, "abc"); errors.Is(err, ErrUnsupported) {
		t.Skip("no store on this system yet")
	} else if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = Delete(name) })
	got, err := Get(name)
	if err != nil || got != "abc" {
		t.Fatalf("%q, %v", got, err)
	}
	if err := Set(name, "another"); err != nil {
		t.Fatal(err)
	}
	if got, _ = Get(name); got != "another" {
		t.Fatalf("replaced: %q", got)
	}
	if err := Delete(name); err != nil {
		t.Fatal(err)
	}
	if _, err := Get(name); !errors.Is(err, ErrNotFound) {
		t.Fatalf("after forgetting: %v", err)
	}
	if err := Delete(name); err != nil {
		t.Fatalf("forgetting twice: %v", err)
	}
}
