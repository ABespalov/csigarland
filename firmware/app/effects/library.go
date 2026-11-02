// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The built-in effects, for the utility to work on with no garland at hand.
// Contributors: None

// Package effects is this folder's files, the ones the firmware's effects pack is made of.
package effects

import (
	"embed"
	"encoding/json"
	"fmt"
)

//go:embed *.json
var files embed.FS

// Effect is one built-in effect, as a file ready to tune.
type Effect struct {
	ID    string
	Name  string
	Group string
	File  []byte
}

// Library lists the effects of the catalog that have a file, in its order. A planned one has none.
func Library() ([]Effect, error) {
	data, err := files.ReadFile("catalog.json")
	if err != nil {
		return nil, fmt.Errorf("read the catalog: %w", err)
	}
	var catalog struct {
		Groups []struct {
			ID   string `json:"id"`
			Name string `json:"name"`
		} `json:"groups"`
		Effects []struct {
			ID    string `json:"id"`
			Group string `json:"group"`
			Todo  bool   `json:"todo"`
		} `json:"effects"`
	}
	if err := json.Unmarshal(data, &catalog); err != nil {
		return nil, fmt.Errorf("read the catalog: %w", err)
	}
	groups := make(map[string]string, len(catalog.Groups))
	for _, group := range catalog.Groups {
		groups[group.ID] = group.Name
	}
	var library []Effect
	for _, entry := range catalog.Effects {
		if entry.Todo {
			continue
		}
		file, err := files.ReadFile(entry.ID + ".json")
		if err != nil {
			return nil, fmt.Errorf("read the effect %s: %w", entry.ID, err)
		}
		var effect struct {
			Name string `json:"name"`
		}
		if err := json.Unmarshal(file, &effect); err != nil {
			return nil, fmt.Errorf("read the effect %s: %w", entry.ID, err)
		}
		library = append(library, Effect{ID: entry.ID, Name: effect.Name, Group: groups[entry.Group], File: file})
	}
	return library, nil
}
