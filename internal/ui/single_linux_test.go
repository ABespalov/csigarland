// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Holds the Linux utility to one window: a second start finds the first and asks it forward.
// Contributors: None

package ui

import (
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// A runtime folder of the test's own, and no utility listening once it ends.
func ownRuntime(t *testing.T) string {
	t.Helper()
	runtime := t.TempDir()
	t.Setenv("XDG_RUNTIME_DIR", runtime)
	t.Cleanup(func() {
		closeInstance()
		instanceShow.Store(nil)
	})
	return runtime
}

func TestASecondStartAsksTheFirstForward(t *testing.T) {
	runtime := ownRuntime(t)
	if got := instancePath(); got != filepath.Join(runtime, "csiGarland", "utility.sock") {
		t.Fatal(got)
	}
	if !firstInstance() {
		t.Fatal("the first start found another")
	}
	if info, err := os.Stat(filepath.Join(runtime, "csiGarland")); err != nil || info.Mode().Perm() != 0o700 {
		t.Fatalf("the folder: %v, %v", info, err)
	}
	shown := make(chan struct{}, 1)
	whenAskedToShow(func() { shown <- struct{}{} })
	if firstInstance() {
		t.Fatal("a second start found none")
	}
	focusFirstInstance()
	select {
	case <-shown:
	case <-time.After(5 * time.Second):
		t.Fatal("the first was not asked forward")
	}
}

// Whatever else comes to the socket shows nothing, and the first goes on answering.
func TestOnlyARequestShowsTheWindow(t *testing.T) {
	ownRuntime(t)
	if !firstInstance() {
		t.Fatal("the first start found another")
	}
	shown := make(chan struct{}, 4)
	whenAskedToShow(func() { shown <- struct{}{} })
	for _, junk := range []string{"open /etc/passwd\n", "showshowshowshowshow\n", "show"} {
		conn, err := net.Dial("unix", instancePath())
		if err != nil {
			t.Fatal(err)
		}
		_, _ = conn.Write([]byte(junk))
		_ = conn.Close()
	}
	focusFirstInstance()
	select {
	case <-shown:
	case <-time.After(5 * time.Second):
		t.Fatal("the request after the junk was not answered")
	}
	select {
	case <-shown:
		t.Fatal("junk showed the window")
	case <-time.After(200 * time.Millisecond):
	}
}

// A socket left by a utility that did not end as it should is no utility.
func TestASocketLeftBehindIsNoUtility(t *testing.T) {
	runtime := ownRuntime(t)
	folder := filepath.Join(runtime, "csiGarland")
	if err := os.MkdirAll(folder, 0o700); err != nil {
		t.Fatal(err)
	}
	stale, err := net.ListenUnix("unix", &net.UnixAddr{Name: instancePath(), Net: "unix"})
	if err != nil {
		t.Fatal(err)
	}
	stale.SetUnlinkOnClose(false)
	_ = stale.Close()
	if _, err := os.Stat(instancePath()); err != nil {
		t.Fatalf("no socket was left: %v", err)
	}
	if !firstInstance() {
		t.Fatal("a socket left behind passed for a utility")
	}
}

// A folder others can reach could be another's, so the utility does not trust it, and opens anyway.
func TestAFolderOthersReachIsNotTrusted(t *testing.T) {
	runtime := ownRuntime(t)
	folder := filepath.Join(runtime, "csiGarland")
	if err := os.MkdirAll(folder, 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(folder, 0o777); err != nil {
		t.Fatal(err)
	}
	if err := ownFolder(folder); err == nil {
		t.Fatal("a folder open to all passed")
	}
	if !firstInstance() {
		t.Fatal("the utility did not open")
	}
	if _, err := os.Stat(instancePath()); err == nil {
		t.Fatal("it listened in a folder open to all")
	}
}
