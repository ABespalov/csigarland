// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: A connection here is let go before the garland closes it as idle.
// Contributors: None

package api

import (
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"testing"
	"time"
)

func TestAConnectionIsLetGoBeforeTheGarlandClosesIt(t *testing.T) {
	header, err := os.ReadFile(filepath.Join("..", "..", "firmware", "app", "src", "portal_watch.h"))
	if err != nil {
		t.Fatal(err)
	}
	found := regexp.MustCompile(`kHeaderDeadlineUs = (\d+) \* 1000 \* 1000;`).FindSubmatch(header)
	if found == nil {
		t.Fatal("no kHeaderDeadlineUs in the firmware")
	}
	seconds, _ := strconv.Atoi(string(found[1]))
	if limit := time.Duration(seconds) * time.Second; idleConnection >= limit-time.Second {
		t.Fatalf("idle for %v here, and the garland closes one idle for %v", idleConnection, limit)
	}
}
