// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Reads what an update file holds, and tells when a garland is running it.
// Contributors: None

package api

import (
	"encoding/binary"
	"encoding/hex"
	"errors"
	"strings"
)

// The layout of firmware/app/src/ota_logic.h.
const (
	updateHeaderBytes  = 148
	updateMaxCertBytes = 1024
	updateMagic        = "CGOT"
	updateFormat       = 3
	updateDigestOffset = 20
	updateDigestBytes  = 32

	imageDescOffset  = 32
	imageDescBytes   = 176
	imageDescMagic   = 0xABCD5432
	imageVersionAt   = 16
	imageVersionSize = 32
	imageHashAt      = 144
	imageHashBytes   = 32

	packMagic      = "CGPK"
	packFormat     = 2
	packHeaderSize = 12
	packPlanned    = 'p'
	packStrings    = 4
)

// UpdateFile is what an update file says it holds. Only the garland checks the signature.
type UpdateFile struct {
	HasFirmware bool
	Version     string // Of the firmware, if it could be read.
	Image       string // The image's hash, to tell it from another of the same version.
	HasEffects  bool
	Effects     int // How many, or -1 for a format this does not read, or a pack cut short.
	PackDigest  string
}

// ParseUpdate reads what an update file holds.
func ParseUpdate(file []byte) (UpdateFile, error) {
	if len(file) < updateHeaderBytes || string(file[:4]) != updateMagic {
		return UpdateFile{}, errors.New("this is not a csiGarland update file")
	}
	if binary.LittleEndian.Uint32(file[4:]) != updateFormat {
		return UpdateFile{}, errors.New("this update file is in a format this utility does not read")
	}
	certificateBytes := binary.LittleEndian.Uint32(file[8:])
	if certificateBytes == 0 || certificateBytes > updateMaxCertBytes {
		return UpdateFile{}, errors.New("this update file has no usable certificate")
	}
	packStart := updateHeaderBytes + uint64(certificateBytes)
	packBytes := uint64(binary.LittleEndian.Uint32(file[12:]))
	imageBytes := uint64(binary.LittleEndian.Uint32(file[16:]))
	if packStart+packBytes+imageBytes != uint64(len(file)) || packBytes+imageBytes == 0 {
		return UpdateFile{}, errors.New("this update file is incomplete")
	}
	var update UpdateFile
	if packBytes > 0 {
		update.HasEffects = true
		update.PackDigest = hex.EncodeToString(file[updateDigestOffset : updateDigestOffset+updateDigestBytes])
		update.Effects = countEffects(file[packStart : packStart+packBytes])
	}
	if imageBytes > 0 {
		update.HasFirmware = true
		readImage(file[packStart+packBytes:], &update)
	}
	return update, nil
}

func readImage(image []byte, update *UpdateFile) {
	if len(image) < imageDescOffset+imageDescBytes {
		return
	}
	desc := image[imageDescOffset : imageDescOffset+imageDescBytes]
	if binary.LittleEndian.Uint32(desc) != imageDescMagic {
		return
	}
	version, _, _ := strings.Cut(string(desc[imageVersionAt:imageVersionAt+imageVersionSize]), "\x00")
	update.Version = version
	update.Image = hex.EncodeToString(desc[imageHashAt : imageHashAt+imageHashBytes])
}

// After the pack's header: a kind letter, then four strings each ending in a NUL.
func countEffects(pack []byte) int {
	if len(pack) < packHeaderSize || string(pack[:4]) != packMagic || binary.LittleEndian.Uint32(pack[4:]) != packFormat ||
		binary.LittleEndian.Uint32(pack[8:]) != uint32(len(pack)) {
		return -1
	}
	count := 0
	for at := packHeaderSize; at < len(pack); {
		if pack[at] != packPlanned {
			count++
		}
		at++
		ended := 0
		for ; ended < packStrings && at < len(pack); at++ {
			if pack[at] == 0 {
				ended++
			}
		}
		if ended < packStrings {
			return -1
		}
	}
	return count
}

// UpdateStage is how an update stands, seen from what the garland reports after sending it.
type UpdateStage int

// The stages, in order.
const (
	UpdateNotRestarted UpdateStage = iota
	UpdateWentBack                 // Another image runs: it went back to the previous firmware.
	UpdateWrongEffects             // Another effects pack is in use.
	UpdateOnTrial                  // Running it, and it has not confirmed itself yet.
	UpdateConfirmed
)

// JudgeUpdate says how an update stands from what the garland reports, given the boot id from
// before the send. A version may repeat between builds, so the image's hash decides.
func JudgeUpdate(update UpdateFile, bootBefore string, now Firmware) UpdateStage {
	switch {
	case now.Boot == bootBefore:
		return UpdateNotRestarted
	case update.Image != "" && now.Image != update.Image:
		return UpdateWentBack
	case update.PackDigest != "" && now.PackDigest != update.PackDigest:
		return UpdateWrongEffects
	case now.OnTrial:
		return UpdateOnTrial
	default:
		return UpdateConfirmed
	}
}
