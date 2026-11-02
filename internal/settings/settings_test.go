// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of keeping the utility's settings.
// Contributors: None

package settings

import (
	"os"
	"path/filepath"
	"slices"
	"testing"
)

func TestSettingsComeBackAsSaved(t *testing.T) {
	path := filepath.Join(t.TempDir(), "folder", "utility.json")
	loaded, err := Load(path)
	if err != nil || len(loaded.Garlands) != 0 {
		t.Fatalf("no file yet: %+v, %v", loaded, err)
	}
	want := Settings{Garlands: []Garland{{Name: "garland", Address: "garland.local", RootPEM: "pem", Fingerprint: "AB", LastUsed: 7}}}
	if err := Save(path, want); err != nil {
		t.Fatal(err)
	}
	if loaded, err = Load(path); err != nil || !slices.Equal(loaded.Garlands, want.Garlands) {
		t.Fatalf("%+v, %v", loaded, err)
	}
	if _, err := os.Stat(path + ".new"); !os.IsNotExist(err) {
		t.Errorf("the partial file stayed: %v", err)
	}
}

func TestTheOneJustUsedIsLastUsedEvenWithinOneSecond(t *testing.T) {
	var kept Settings
	kept.Remember(Garland{Fingerprint: "A", LastUsed: 5})
	kept.Remember(Garland{Fingerprint: "B", LastUsed: 5})
	if last, _ := kept.Last(); last.Fingerprint != "B" {
		t.Fatalf("%+v", kept.Garlands)
	}
	// A clock that went back changes nothing either.
	kept.Remember(Garland{Fingerprint: "A", LastUsed: 1})
	if last, _ := kept.Last(); last.Fingerprint != "A" {
		t.Fatalf("%+v", kept.Garlands)
	}
}

func TestTheLastGarlandUsedComesFirst(t *testing.T) {
	var kept Settings
	if _, ok := kept.Last(); ok {
		t.Fatal("a last garland out of nothing")
	}
	kept.Remember(Garland{Fingerprint: "A", Address: "old", LastUsed: 1})
	kept.Remember(Garland{Fingerprint: "B", LastUsed: 2})
	if last, _ := kept.Last(); last.Fingerprint != "B" {
		t.Fatalf("%+v", kept)
	}
	kept.Remember(Garland{Fingerprint: "A", Address: "new", LastUsed: 3})
	if last, _ := kept.Last(); last.Fingerprint != "A" || last.Address != "new" || len(kept.Garlands) != 2 {
		t.Fatalf("%+v", kept)
	}
	kept.Forget("A")
	if _, known := kept.Find("A"); known || len(kept.Garlands) != 1 {
		t.Fatalf("%+v", kept)
	}
}

func TestADamagedFileIsSaidToBe(t *testing.T) {
	path := filepath.Join(t.TempDir(), "utility.json")
	if err := os.WriteFile(path, []byte("{"), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := Load(path); err == nil {
		t.Fatal("a damaged file was taken")
	}
}
