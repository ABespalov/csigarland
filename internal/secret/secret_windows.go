// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The token store on Windows: Credential Manager.
// Contributors: None

package secret

import (
	"errors"
	"fmt"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

const (
	credTypeGeneric          = 1
	credPersistLocal         = 2
	errorNotFound    uintptr = 1168
)

var (
	advapi32    = windows.NewLazySystemDLL("advapi32.dll")
	procWrite   = advapi32.NewProc("CredWriteW")
	procRead    = advapi32.NewProc("CredReadW")
	procDelete  = advapi32.NewProc("CredDeleteW")
	procRelease = advapi32.NewProc("CredFree")
)

// As wincred.h has it.
type credential struct {
	Flags              uint32
	Type               uint32
	TargetName         *uint16
	Comment            *uint16
	LastWritten        windows.Filetime
	CredentialBlobSize uint32
	CredentialBlob     *byte
	Persist            uint32
	AttributeCount     uint32
	Attributes         uintptr
	TargetAlias        *uint16
	UserName           *uint16
}

func callError(result uintptr, callErr error) error {
	if result != 0 {
		return nil
	}
	var number syscall.Errno
	if errors.As(callErr, &number) && uintptr(number) == errorNotFound {
		return ErrNotFound
	}
	return callErr
}

// Set keeps a token under a name, replacing what was there.
func Set(name, token string) error {
	if token == "" {
		return errors.New("no token to keep")
	}
	target, err := windows.UTF16PtrFromString(targetPrefix + name)
	if err != nil {
		return fmt.Errorf("name the token: %w", err)
	}
	blob := []byte(token)
	entry := credential{
		Type:               credTypeGeneric,
		TargetName:         target,
		CredentialBlobSize: uint32(len(blob)),
		CredentialBlob:     &blob[0],
		Persist:            credPersistLocal,
	}
	result, _, callErr := procWrite.Call(uintptr(unsafe.Pointer(&entry)), 0)
	if err := callError(result, callErr); err != nil {
		return fmt.Errorf("keep the token: %w", err)
	}
	return nil
}

// Get reads the token kept under a name, or ErrNotFound.
func Get(name string) (string, error) {
	target, err := windows.UTF16PtrFromString(targetPrefix + name)
	if err != nil {
		return "", fmt.Errorf("name the token: %w", err)
	}
	var entry *credential
	result, _, callErr := procRead.Call(uintptr(unsafe.Pointer(target)), credTypeGeneric, 0, uintptr(unsafe.Pointer(&entry)))
	if err := callError(result, callErr); err != nil {
		if errors.Is(err, ErrNotFound) {
			return "", err
		}
		return "", fmt.Errorf("read the token: %w", err)
	}
	defer func() { _, _, _ = procRelease.Call(uintptr(unsafe.Pointer(entry))) }()
	return string(unsafe.Slice(entry.CredentialBlob, entry.CredentialBlobSize)), nil
}

// Delete forgets the token under a name. One not there is no failure.
func Delete(name string) error {
	target, err := windows.UTF16PtrFromString(targetPrefix + name)
	if err != nil {
		return fmt.Errorf("name the token: %w", err)
	}
	result, _, callErr := procDelete.Call(uintptr(unsafe.Pointer(target)), credTypeGeneric, 0)
	if err := callError(result, callErr); err != nil && !errors.Is(err, ErrNotFound) {
		return fmt.Errorf("forget the token: %w", err)
	}
	return nil
}
