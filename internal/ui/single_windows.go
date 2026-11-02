// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: One window at a time on Windows: a second start brings the first to the front and ends.
// Contributors: None

package ui

import (
	"errors"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"
)

const (
	instanceMutex  = `Local\csiGarlandUtility`
	showRestore    = 9
	findTries      = 25 // The first may have started only a moment ago, with no window yet.
	findTriesEvery = 200 * time.Millisecond
)

// Held for as long as the process runs.
var instanceHandle windows.Handle

var (
	user32            = windows.NewLazySystemDLL("user32.dll")
	procFindWindow    = user32.NewProc("FindWindowW")
	procIsIconic      = user32.NewProc("IsIconic")
	procShowWindow    = user32.NewProc("ShowWindow")
	procSetForeground = user32.NewProc("SetForegroundWindow")
)

// firstInstance reports whether no other utility runs. The mutex is left held, and goes with the
// process. If it cannot be made at all, a second window is better than none.
func firstInstance() bool {
	var err error
	instanceHandle, err = windows.CreateMutex(nil, false, windows.StringToUTF16Ptr(instanceMutex))
	return !errors.Is(err, windows.ERROR_ALREADY_EXISTS)
}

// focusFirstInstance brings the other utility's window to the front.
func focusFirstInstance() {
	// By class as well: a folder of this name in Explorer can have the same title.
	class := windows.StringToUTF16Ptr(windowClass)
	title := windows.StringToUTF16Ptr(windowTitle)
	for range findTries {
		window, _, _ := procFindWindow.Call(uintptr(unsafe.Pointer(class)), uintptr(unsafe.Pointer(title)))
		if window != 0 {
			if minimized, _, _ := procIsIconic.Call(window); minimized != 0 {
				_, _, _ = procShowWindow.Call(window, showRestore)
			}
			_, _, _ = procSetForeground.Call(window)
			return
		}
		time.Sleep(findTriesEvery)
	}
}
