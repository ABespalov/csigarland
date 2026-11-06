// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Choosing a file where no dialog is written, on a system other than Windows, Linux and macOS.
// Contributors: None

//go:build !windows && !linux && !darwin

package ui

import (
	"errors"

	"gioui.org/app"
)

var errNoFileDialog = errors.New("choosing a file is not written for this system yet")

func viewHandle(app.ViewEvent) uintptr { return 0 }

func chooseFile(uintptr, string, string, string) (string, error) {
	return "", errNoFileDialog
}

func chooseSaveFile(uintptr, string, string, string, string, string) (string, error) {
	return "", errNoFileDialog
}
