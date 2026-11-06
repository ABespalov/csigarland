// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the file dialogs on macOS, short of showing one.
// Contributors: None

package ui

import (
	"testing"

	"gioui.org/app"
)

func TestADialogBelongsToTheAppKitView(t *testing.T) {
	if got := viewHandle(app.AppKitViewEvent{View: 42, Layer: 7}); got != 42 {
		t.Fatalf("%d", got)
	}
	if got := viewHandle(app.AppKitViewEvent{}); got != 0 {
		t.Fatalf("a window gone: %d", got)
	}
}

func TestAPatternNamesTheExtensionThePanelAllows(t *testing.T) {
	for pattern, want := range map[string]string{
		"*.json": "json", "*.ota": "ota", "*": "", "": "", "*.": "", "glow.json": "", "*.j*": "",
	} {
		if got := patternExtension(pattern); got != want {
			t.Errorf("%q: %q, want %q", pattern, got, want)
		}
	}
}

func TestAWholePathStartsTheSaveInItsFolder(t *testing.T) {
	if folder, name := saveStart("glow.json"); folder != "" || name != "glow.json" {
		t.Fatalf("%q %q", folder, name)
	}
	if folder, name := saveStart("/Users/owner/effects/glow.json"); folder != "/Users/owner/effects" || name != "glow.json" {
		t.Fatalf("%q %q", folder, name)
	}
	if folder, name := saveStart(""); folder != "" || name != "" {
		t.Fatalf("%q %q", folder, name)
	}
}
