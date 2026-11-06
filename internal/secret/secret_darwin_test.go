// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the token store on macOS.
// Contributors: None

package secret

import (
	"errors"
	"strings"
	"testing"
)

func TestAKeychainStatusSaysWhatItMeans(t *testing.T) {
	if err := statusError(0); err != nil {
		t.Fatalf("success: %v", err)
	}
	if err := statusError(-25300); !errors.Is(err, ErrNotFound) { // errSecItemNotFound
		t.Fatalf("not found: %v", err)
	}
	if err := statusError(-25307); !errors.Is(err, ErrUnsupported) { // errSecNoDefaultKeychain
		t.Fatalf("no keychain: %v", err)
	}
	err := statusError(-128) // errSecUserCanceled: the owner refused the utility the item.
	if err == nil || errors.Is(err, ErrNotFound) || !strings.Contains(err.Error(), "-128") {
		t.Fatalf("cancelled: %v", err)
	}
}

func TestATokenOfAnyBytesComesBackWhole(t *testing.T) {
	const name = "test-of-the-store-any-bytes"
	token := "a\x00b\xffc " + strings.Repeat("x", 4096)
	if err := Set(name, token); errors.Is(err, ErrUnsupported) {
		t.Skip("no keychain in this session")
	} else if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = Delete(name) })
	if got, err := Get(name); err != nil || got != token {
		t.Fatalf("%d bytes back of %d: %v", len(got), len(token), err)
	}
}

func TestNoTokenIsNotKept(t *testing.T) {
	if err := Set("test-of-the-store-nothing", ""); err == nil {
		t.Fatal("an empty token was kept")
	}
}
