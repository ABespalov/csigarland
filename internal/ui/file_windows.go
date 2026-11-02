// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The system's own dialogs for choosing a file to open or to save, on Windows.
// Contributors: None

package ui

import (
	"errors"
	"fmt"
	"runtime"
	"unicode/utf16"
	"unsafe"

	"gioui.org/app"
	"golang.org/x/sys/windows"
)

const (
	openFileMustExist   uint32 = 0x00001000
	openPathMustExist   uint32 = 0x00000800
	openNoChangeDir     uint32 = 0x00000008
	saveOverwritePrompt uint32 = 0x00000002
	maxPathChars               = 4096
)

var (
	comdlg32          = windows.NewLazySystemDLL("comdlg32.dll")
	procGetOpenName   = comdlg32.NewProc("GetOpenFileNameW")
	procGetSaveName   = comdlg32.NewProc("GetSaveFileNameW")
	procDialogFailure = comdlg32.NewProc("CommDlgExtendedError")
)

// As commdlg.h has it.
type openFileName struct {
	StructSize      uint32
	Owner           uintptr
	Instance        uintptr
	Filter          *uint16
	CustomFilter    *uint16
	MaxCustomFilter uint32
	FilterIndex     uint32
	File            *uint16
	MaxFile         uint32
	FileTitle       *uint16
	MaxFileTitle    uint32
	InitialDir      *uint16
	Title           *uint16
	Flags           uint32
	FileOffset      uint16
	FileExtension   uint16
	DefExt          *uint16
	CustData        uintptr
	FnHook          uintptr
	TemplateName    *uint16
	PvReserved      uintptr
	DwReserved      uint32
	FlagsEx         uint32
}

// dialogFilter is what the dialog wants: pairs of a description and a pattern, each ending in a NUL,
// and one more NUL after the last. UTF16FromString refuses a NUL inside its string, so it is built
// here from its parts.
func dialogFilter(description, pattern string) []uint16 {
	var filter []uint16
	for _, part := range []string{description, pattern, "All files", "*.*"} {
		filter = append(filter, utf16.Encode([]rune(part))...)
		filter = append(filter, 0)
	}
	return append(filter, 0)
}

// viewHandle is the window's own, for a dialog to open over it rather than anywhere.
func viewHandle(event app.ViewEvent) uintptr {
	if view, isWin32 := event.(app.Win32ViewEvent); isWin32 {
		return view.HWND
	}
	return 0
}

func runFileDialog(owner uintptr, procedure *windows.LazyProc, title, pattern, description, extension, startName string, flags uint32) (string, error) {
	// The dialog's windows belong to the thread that made them, for as long as it is open.
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	filter := dialogFilter(description, pattern)
	name, err := windows.UTF16PtrFromString(title)
	if err != nil {
		return "", fmt.Errorf("the dialog's title: %w", err)
	}
	path := make([]uint16, maxPathChars)
	start, err := windows.UTF16FromString(startName)
	if err != nil {
		return "", fmt.Errorf("the file's name: %w", err)
	}
	copy(path[:maxPathChars-1], start)
	dialog := openFileName{
		Owner:   owner,
		Filter:  &filter[0],
		File:    &path[0],
		MaxFile: uint32(len(path)),
		Title:   name,
		Flags:   flags | openPathMustExist | openNoChangeDir,
	}
	if extension != "" {
		if dialog.DefExt, err = windows.UTF16PtrFromString(extension); err != nil {
			return "", fmt.Errorf("the file's extension: %w", err)
		}
	}
	dialog.StructSize = uint32(unsafe.Sizeof(dialog))
	result, _, _ := procedure.Call(uintptr(unsafe.Pointer(&dialog)))
	if result != 0 {
		return windows.UTF16ToString(path), nil
	}
	if failure, _, _ := procDialogFailure.Call(); failure != 0 {
		return "", errors.New("the system's file dialog failed")
	}
	return "", nil
}

// chooseFile asks, over the window whose handle is owner, for a file to open, and answers "" if the
// owner cancelled. It waits, so call it away from the window's goroutine.
func chooseFile(owner uintptr, title, pattern, description string) (string, error) {
	return runFileDialog(owner, procGetOpenName, title, pattern, description, "", "", openFileMustExist)
}

// chooseSaveFile asks where to save a file, as chooseFile does.
func chooseSaveFile(owner uintptr, title, pattern, description, extension, startName string) (string, error) {
	return runFileDialog(owner, procGetSaveName, title, pattern, description, extension, startName, saveOverwritePrompt)
}
