// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: One window at a time on Linux: the first listens on a socket of this user's, and a second
// start asks it there to come to the front, and ends.
// Contributors: None

package ui

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/rs/zerolog/log"
)

const (
	instanceFolder  = "csiGarland"
	instanceSocket  = "utility.sock"
	instanceRequest = "show"
	instanceWait    = time.Second
	instanceLimit   = 16
)

var (
	instanceMu       sync.Mutex
	instanceListener net.Listener // Held for as long as the process runs.
	instanceShow     atomic.Pointer[func()]
)

// instancePath is the socket, in the folder the session gives this user alone, or one of the user's own
// in the temporary folder where there is none.
func instancePath() string {
	folder := filepath.Join(os.TempDir(), fmt.Sprintf("%s-%d", instanceFolder, os.Getuid()))
	if runtime := os.Getenv("XDG_RUNTIME_DIR"); runtime != "" {
		folder = filepath.Join(runtime, instanceFolder)
	}
	return filepath.Join(folder, instanceSocket)
}

// ownFolder makes the socket's folder, and holds it to this user alone: in a shared temporary folder
// another could have made it first, to answer in the utility's place.
func ownFolder(folder string) error {
	if err := os.MkdirAll(folder, 0o700); err != nil {
		return fmt.Errorf("make the folder of the utility's socket: %w", err)
	}
	info, err := os.Lstat(folder)
	if err != nil {
		return fmt.Errorf("look at the folder of the utility's socket: %w", err)
	}
	stat, isUnix := info.Sys().(*syscall.Stat_t)
	if !info.IsDir() || !isUnix || int(stat.Uid) != os.Getuid() || info.Mode().Perm()&0o077 != 0 {
		return fmt.Errorf("%s is not this user's alone", folder)
	}
	return nil
}

// firstInstance reports whether no other utility runs, and if none does, listens for those started
// after it. If it cannot listen at all, a second window is better than none.
func firstInstance() bool {
	instanceMu.Lock()
	defer instanceMu.Unlock()
	path := instancePath()
	if err := ownFolder(filepath.Dir(path)); err != nil {
		log.Warn().Err(err).Msg("cannot keep to one window")
		return true
	}
	listener, err := net.Listen("unix", path)
	if err != nil {
		if conn, dialErr := net.DialTimeout("unix", path, instanceWait); dialErr == nil {
			_ = conn.Close()
			return false
		}
		// Left by a utility that did not end as it should: nothing listens there.
		if removeErr := os.Remove(path); removeErr != nil && !errors.Is(removeErr, fs.ErrNotExist) {
			log.Warn().Err(removeErr).Msg("cannot keep to one window")
			return true
		}
		if listener, err = net.Listen("unix", path); err != nil {
			log.Warn().Err(err).Msg("cannot keep to one window")
			return true
		}
	}
	instanceListener = listener
	go serveInstance(listener)
	return true
}

// serveInstance answers those started after this one, until the listener is closed.
func serveInstance(listener net.Listener) {
	for {
		conn, err := listener.Accept()
		if err != nil {
			return
		}
		_ = conn.SetDeadline(time.Now().Add(instanceWait))
		// No more than a request can be, whatever another sends.
		line, err := bufio.NewReader(io.LimitReader(conn, instanceLimit)).ReadString('\n')
		_ = conn.Close()
		if err != nil || line != instanceRequest+"\n" {
			continue
		}
		if show := instanceShow.Load(); show != nil {
			(*show)()
		}
	}
}

// closeInstance stops listening, so that another utility may start.
func closeInstance() {
	instanceMu.Lock()
	defer instanceMu.Unlock()
	if instanceListener != nil {
		_ = instanceListener.Close()
		instanceListener = nil
	}
}

// whenAskedToShow is what this window does when another start asks it to come to the front.
func whenAskedToShow(show func()) { instanceShow.Store(&show) }

// focusFirstInstance asks the other utility to come to the front. On X11 it does; on Wayland the
// compositor decides, and GNOME's keeps it where it is.
func focusFirstInstance() {
	conn, err := net.DialTimeout("unix", instancePath(), instanceWait)
	if err != nil {
		return
	}
	defer func() { _ = conn.Close() }()
	_ = conn.SetDeadline(time.Now().Add(instanceWait))
	_, _ = conn.Write([]byte(instanceRequest + "\n"))
}
