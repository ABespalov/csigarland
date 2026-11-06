// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The token store where none is written, on a system other than Windows, Linux and macOS.
// Contributors: None

//go:build !windows && !linux && !darwin

package secret

// Set refuses, as a token must not go anywhere less safe than the system's store.
func Set(_, _ string) error { return ErrUnsupported }

// Get finds nothing.
func Get(_ string) (string, error) { return "", ErrNotFound }

// Delete has nothing to do.
func Delete(_ string) error { return nil }
