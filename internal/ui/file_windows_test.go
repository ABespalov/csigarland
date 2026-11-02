// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Holds the file dialog's filter to the form Windows wants.
// Contributors: None

package ui

import (
	"slices"
	"testing"
	"unicode/utf16"

	"gioui.org/app"
)

// A dialog opens over the window, whose handle comes with its view.
func TestADialogBelongsToTheWindow(t *testing.T) {
	if got := viewHandle(app.Win32ViewEvent{HWND: 42}); got != 42 {
		t.Fatalf("%d", got)
	}
	if got := viewHandle(app.Win32ViewEvent{}); got != 0 {
		t.Fatalf("a window gone left %d", got)
	}
}

func TestAFileDialogsFilterEndsEachPartInANul(t *testing.T) {
	want := utf16.Encode([]rune("Effects\x00*.json\x00All files\x00*.*\x00\x00"))
	if got := dialogFilter("Effects", "*.json"); !slices.Equal(got, want) {
		t.Fatalf("%v", got)
	}
}
