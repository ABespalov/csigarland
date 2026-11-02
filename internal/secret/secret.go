// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Keeps a garland's token in the system's own secure store.
// Contributors: None

// Package secret keeps tokens where the system keeps its own, never in a file of the utility's.
package secret

import "errors"

// ErrNotFound says no token is kept under that name.
var ErrNotFound = errors.New("no token is kept for this garland")

// ErrUnsupported says this system's store is not written yet.
var ErrUnsupported = errors.New("this system has no token store yet")

const targetPrefix = "csiGarland/"
