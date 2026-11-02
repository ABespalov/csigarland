// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Holds the library to what the firmware's pack is made of.
// Contributors: None

package effects

import "testing"

func TestTheLibraryIsTheCatalog(t *testing.T) {
	library, err := Library()
	if err != nil {
		t.Fatal(err)
	}
	if len(library) == 0 {
		t.Fatal("no effects")
	}
	seen := map[string]bool{}
	for _, effect := range library {
		if effect.Name == "" || effect.Group == "" || len(effect.File) == 0 || seen[effect.ID] {
			t.Errorf("%+v", effect.ID)
		}
		seen[effect.ID] = true
	}
}
