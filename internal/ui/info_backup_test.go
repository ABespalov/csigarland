// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of writing a backup file whole, by one save or by several at once.
// Contributors: None

package ui

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"testing"
)

// Several saves to one path at once: the file is one of theirs, whole, and nothing is left beside it.
func TestBackupsSavedAtOnceEachPutTheirOwnBytes(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "garland.backup.json")
	if err := os.WriteFile(path, []byte("before"), 0o600); err != nil {
		t.Fatal(err)
	}
	var wanted [][]byte
	for i := range 8 {
		wanted = append(wanted, bytes.Repeat([]byte(fmt.Sprintf("save %d;", i)), 4096))
	}
	var group sync.WaitGroup
	errs := make([]error, len(wanted))
	for i, data := range wanted {
		group.Add(1)
		go func() {
			defer group.Done()
			errs[i] = writeWhole(path, data)
		}()
	}
	group.Wait()
	written, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	whole := false
	for i, data := range wanted {
		// A rename may lose to another on Windows; a save that says it failed left nothing of its own.
		if errs[i] == nil && bytes.Equal(written, data) {
			whole = true
		}
	}
	if !whole {
		t.Fatalf("the file is no one save's bytes whole: %d bytes, errors %v", len(written), errs)
	}
	left, err := filepath.Glob(filepath.Join(dir, "*.new"))
	if err != nil || len(left) != 0 {
		t.Fatalf("left beside it: %v %v", left, err)
	}
}
