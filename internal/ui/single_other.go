// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: One window at a time, where it is not written yet, as on macOS.
// Contributors: None

//go:build !windows && !linux

package ui

func firstInstance() bool { return true }

func focusFirstInstance() {}

func whenAskedToShow(func()) {}
