// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Holds the Linux file dialogs to what the desktop portal and zenity want and answer.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"slices"
	"testing"

	"gioui.org/app"
	"github.com/godbus/dbus/v5"
)

// On X11 a dialog opens over the window, which the portal knows by its id in hex. Wayland names none.
func TestADialogBelongsToTheX11Window(t *testing.T) {
	if got := viewHandle(app.X11ViewEvent{Window: 0x2a00007}); got != 0x2a00007 {
		t.Fatalf("%x", got)
	}
	if got := viewHandle(app.WaylandViewEvent{}); got != 0 {
		t.Fatalf("wayland: %x", got)
	}
	if got := parentWindow(0x2a00007); got != "x11:2a00007" {
		t.Fatal(got)
	}
	if got := parentWindow(0); got != "" {
		t.Fatal(got)
	}
}

func TestThePortalsAnswerComesFromAPathKnownAhead(t *testing.T) {
	if got := requestPath(":1.234", "csigarland_ab"); got != "/org/freedesktop/portal/desktop/request/1_234/csigarland_ab" {
		t.Fatal(got)
	}
	token, err := newToken()
	if err != nil {
		t.Fatal(err)
	}
	if !dbus.ObjectPath("/a/" + token).IsValid() {
		t.Fatalf("%q is no path element", token)
	}
}

func TestAPortalFilterOffersAllFilesToo(t *testing.T) {
	got := portalFilters("Effect files (*.json)", "*.json")
	if len(got) != 2 || got[0].Name != "Effect files (*.json)" || got[0].Patterns[0] != (portalPattern{0, "*.json"}) ||
		got[1].Patterns[0].Pattern != "*" {
		t.Fatalf("%+v", got)
	}
	if signature := dbus.SignatureOf(got).String(); signature != "a(sa(us))" {
		t.Fatalf("signature %s", signature)
	}
}

func TestThePortalsAnswerNamesAFileOrNothing(t *testing.T) {
	chosen := map[string]dbus.Variant{"uris": dbus.MakeVariant([]string{"file:///home/owner/My%20effects/glow.json"})}
	if got, err := portalAnswer([]any{uint32(0), chosen}); err != nil || got != "/home/owner/My effects/glow.json" {
		t.Fatalf("%q, %v", got, err)
	}
	if got, err := portalAnswer([]any{uint32(portalCancelled), map[string]dbus.Variant{}}); err != nil || got != "" {
		t.Fatalf("cancelled: %q, %v", got, err)
	}
	if _, err := portalAnswer([]any{uint32(2), map[string]dbus.Variant{}}); err == nil {
		t.Fatal("a failure passed")
	}
	remote := map[string]dbus.Variant{"uris": dbus.MakeVariant([]string{"sftp://host/glow.json"})}
	if _, err := portalAnswer([]any{uint32(0), remote}); err == nil {
		t.Fatal("a remote file passed")
	}
	if _, err := portalAnswer([]any{uint32(0)}); err == nil {
		t.Fatal("an answer without results passed")
	}
}

func TestANameSavedWithoutAnExtensionGetsOne(t *testing.T) {
	for in, want := range map[string]string{
		"/home/owner/glow":      "/home/owner/glow.json",
		"/home/owner/glow.json": "/home/owner/glow.json",
		"/home/owner/glow.txt":  "/home/owner/glow.txt",
		"":                      "",
	} {
		if got := withExtension(in, "json"); got != want {
			t.Errorf("%q: %q", in, got)
		}
	}
}

func TestZenityIsAskedAsTheWindowsDialogIs(t *testing.T) {
	open := zenityArgs(false, "Effect file", "*.json", "Effect files (*.json)", "")
	if !slices.Equal(open, []string{"--file-selection", "--title=Effect file",
		"--file-filter=Effect files (*.json) | *.json", "--file-filter=All files | *"}) {
		t.Fatalf("%q", open)
	}
	save := zenityArgs(true, "Save", "*.json", "Backups (*.json)", "garland.json")
	if !slices.Contains(save, "--save") || !slices.Contains(save, "--confirm-overwrite") || !slices.Contains(save, "--filename=garland.json") {
		t.Fatalf("%q", save)
	}
}

// A name typed without the extension, whose file with it is there, is asked about again by its whole name, so
// that the dialog asks whether to replace that one; with none there, it is taken at once.
func TestASaveOverAFileWithTheExtensionIsAskedAgain(t *testing.T) {
	folder := t.TempDir()
	if err := os.WriteFile(filepath.Join(folder, "glow.json"), []byte("{}"), 0o600); err != nil {
		t.Fatal(err)
	}
	var asked []string
	answers := []string{filepath.Join(folder, "glow"), filepath.Join(folder, "glow.json")}
	saved := saveDialog
	t.Cleanup(func() { saveDialog = saved })
	saveDialog = func(_ context.Context, _ uintptr, _, _, _, startName string) (string, error) {
		asked = append(asked, startName)
		answer := answers[0]
		answers = answers[1:]
		return answer, nil
	}
	got, err := chooseSaveFile(context.Background(), 0, "Save", "*.json", "Effects", "json", "start.json")
	if err != nil || got != filepath.Join(folder, "glow.json") {
		t.Fatalf("%q, %v", got, err)
	}
	if !slices.Equal(asked, []string{"start.json", filepath.Join(folder, "glow.json")}) {
		t.Fatalf("asked %q", asked)
	}

	asked = nil
	answers = []string{filepath.Join(folder, "new")}
	if got, err := chooseSaveFile(context.Background(), 0, "Save", "*.json", "Effects", "json", "start.json"); err != nil || got != filepath.Join(folder, "new.json") || len(asked) != 1 {
		t.Fatalf("a new name: %q, %v, asked %q", got, err, asked)
	}
	answers = []string{""}
	if got, err := chooseSaveFile(context.Background(), 0, "Save", "*.json", "Effects", "json", "start.json"); err != nil || got != "" {
		t.Fatalf("cancelled: %q, %v", got, err)
	}
	// The utility ending ends the dialog, and none is asked for after it.
	ended, end := context.WithCancel(context.Background())
	end()
	asked = nil
	if got, err := chooseSaveFile(ended, 0, "Save", "*.json", "Effects", "json", "start.json"); !errors.Is(err, context.Canceled) || got != "" || len(asked) != 0 {
		t.Fatalf("ended: %q, %v, asked %q", got, err, asked)
	}
	if _, err := chooseFile(ended, 0, "Open", "*.json", "Effects"); !errors.Is(err, context.Canceled) {
		t.Fatalf("a dialog to open was asked for after the utility ended: %v", err)
	}
	if _, err := zenityChoose(ended, false, "Open", "*.json", "Effects", ""); err == nil {
		t.Fatal("zenity asked after the utility ended")
	}
}

func TestAWholePathStartsTheSaveInItsFolder(t *testing.T) {
	if name, folder := saveStart("glow.json"); name != "glow.json" || folder != nil {
		t.Fatalf("%q %q", name, folder)
	}
	name, folder := saveStart("/home/owner/effects/glow.json")
	if name != "glow.json" || string(folder) != "/home/owner/effects\x00" {
		t.Fatalf("%q %q", name, folder)
	}
}
