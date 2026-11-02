// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: What the utility remembers between runs, other than the tokens.
// Contributors: None

// Package settings keeps which garlands the utility has talked to, in the user's configuration folder.
package settings

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
)

const (
	folderName = "csiGarland"
	fileName   = "utility.json"
)

// Garland is one the utility has been connected to. Its root's fingerprint is what says which one it
// is, as its address may change. Its token is not here: see package secret.
type Garland struct {
	Name        string `json:"name,omitempty"`
	Address     string `json:"address"`
	RootPEM     string `json:"root_pem"`
	Fingerprint string `json:"fingerprint"`
	LastUsed    int64  `json:"last_used,omitempty"` // Seconds since 1970.
}

// Settings is the file's whole content.
type Settings struct {
	Garlands []Garland `json:"garlands,omitempty"` // The last used first.
}

// Last is the garland connected to most recently.
func (s Settings) Last() (Garland, bool) {
	if len(s.Garlands) == 0 {
		return Garland{}, false
	}
	return s.Garlands[0], true
}

// Find is the garland with this root.
func (s Settings) Find(fingerprint string) (Garland, bool) {
	for _, garland := range s.Garlands {
		if garland.Fingerprint == fingerprint {
			return garland, true
		}
	}
	return Garland{}, false
}

// Remember keeps a garland, in place of what was known of it, as the last used. That goes by when this
// was called, as a clock can repeat a second or go back.
func (s *Settings) Remember(garland Garland) {
	s.Forget(garland.Fingerprint)
	s.Garlands = slices.Insert(s.Garlands, 0, garland)
}

// Forget drops a garland.
func (s *Settings) Forget(fingerprint string) {
	s.Garlands = slices.DeleteFunc(s.Garlands, func(garland Garland) bool { return garland.Fingerprint == fingerprint })
}

// DefaultPath is where the settings live for this user.
func DefaultPath() (string, error) {
	folder, err := os.UserConfigDir()
	if err != nil {
		return "", fmt.Errorf("find the configuration folder: %w", err)
	}
	return filepath.Join(folder, folderName, fileName), nil
}

// Load reads the settings. A file not there yet is no failure.
func Load(path string) (Settings, error) {
	data, err := os.ReadFile(path)
	if errors.Is(err, fs.ErrNotExist) {
		return Settings{}, nil
	}
	if err != nil {
		return Settings{}, fmt.Errorf("read the settings: %w", err)
	}
	var loaded Settings
	if err := json.Unmarshal(data, &loaded); err != nil {
		return Settings{}, fmt.Errorf("read the settings: %w", err)
	}
	return loaded, nil
}

// Save writes the settings whole, through a file beside them, so a failure leaves the old ones.
func Save(path string, values Settings) error {
	data, err := json.MarshalIndent(values, "", "  ")
	if err != nil {
		return fmt.Errorf("write the settings: %w", err)
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return fmt.Errorf("make the settings folder: %w", err)
	}
	partial := path + ".new"
	if err := os.WriteFile(partial, data, 0o600); err != nil {
		return fmt.Errorf("write the settings: %w", err)
	}
	if err := os.Rename(partial, path); err != nil {
		return fmt.Errorf("write the settings: %w", err)
	}
	return nil
}
