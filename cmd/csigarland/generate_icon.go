// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Makes app.ico: a pixel cG, in the wordmark's letters and colours, on the window's slate.
// Contributors: None

//go:build ignore

// From cmd/csigarland, and then the resource that puts it in the program, with csigarland.manifest:
//
//	go run generate_icon.go
//	go run github.com/akavel/rsrc@v0.10.2 -arch amd64 -ico app.ico -manifest csigarland.manifest -o rsrc_windows_amd64.syso
package main

import (
	"encoding/binary"
	"os"

	"github.com/rs/zerolog/log"
)

const (
	columnsWide  = 11 // Two letters of five, and one between.
	rowsTall     = 7
	gridFor      = 16 // Whole pixels to a cell: one at 16, sixteen at 256.
	bytesPerRGBA = 4
	bitmapHeader = 40
	fileHeader   = 6
	entryBytes   = 16
)

var (
	sizes = []int{16, 24, 32, 48, 64, 128, 256}
	// Five column bytes per letter, bit 0 the top row, as firmware/app/src/wordmark.h has them.
	letterC = [5]byte{56, 68, 68, 68, 32}
	letterG = [5]byte{62, 65, 73, 73, 122}
	// Blue, green, red and alpha, as an icon's pixels are.
	slate = [bytesPerRGBA]byte{0x33, 0x20, 0x15, 0xFF}
	white = [bytesPerRGBA]byte{0xFC, 0xFA, 0xF8, 0xFF}
	blue  = [bytesPerRGBA]byte{0xF6, 0x82, 0x3B, 0xFF}
)

func paint(size int) []byte {
	pixels := make([]byte, size*size*bytesPerRGBA)
	for i := range size * size {
		copy(pixels[i*bytesPerRGBA:], slate[:])
	}
	scale := max(size/gridFor, 1)
	left := (size - columnsWide*scale) / 2
	top := (size - rowsTall*scale) / 2
	letter := func(glyph [5]byte, startColumn int, ink [bytesPerRGBA]byte) {
		for column, bits := range glyph {
			for row := range rowsTall {
				if bits&(1<<row) == 0 {
					continue
				}
				for dy := range scale {
					for dx := range scale {
						x := left + (startColumn+column)*scale + dx
						y := top + row*scale + dy
						copy(pixels[(y*size+x)*bytesPerRGBA:], ink[:])
					}
				}
			}
		}
	}
	letter(letterC, 0, white)
	letter(letterG, 6, blue)
	return pixels
}

// A bitmap as an icon holds it: bottom row first, twice as tall for its mask, which stays empty.
func bitmap(size int) []byte {
	pixels := paint(size)
	rowBytes := size * bytesPerRGBA
	maskRow := (size + 31) / 32 * 4
	out := make([]byte, bitmapHeader+rowBytes*size+maskRow*size)
	binary.LittleEndian.PutUint32(out[0:], bitmapHeader)
	binary.LittleEndian.PutUint32(out[4:], uint32(size))
	binary.LittleEndian.PutUint32(out[8:], uint32(size*2))
	binary.LittleEndian.PutUint16(out[12:], 1)
	binary.LittleEndian.PutUint16(out[14:], bytesPerRGBA*8)
	for y := range size {
		copy(out[bitmapHeader+y*rowBytes:], pixels[(size-1-y)*rowBytes:(size-y)*rowBytes])
	}
	return out
}

func main() {
	out := make([]byte, fileHeader+entryBytes*len(sizes))
	binary.LittleEndian.PutUint16(out[2:], 1)
	binary.LittleEndian.PutUint16(out[4:], uint16(len(sizes)))
	for i, size := range sizes {
		image := bitmap(size)
		entry := out[fileHeader+i*entryBytes:]
		// 256 is written as 0.
		entry[0], entry[1] = byte(size), byte(size)
		binary.LittleEndian.PutUint16(entry[4:], 1)
		binary.LittleEndian.PutUint16(entry[6:], bytesPerRGBA*8)
		binary.LittleEndian.PutUint32(entry[8:], uint32(len(image)))
		binary.LittleEndian.PutUint32(entry[12:], uint32(len(out)))
		out = append(out, image...)
	}
	if err := os.WriteFile("app.ico", out, 0o644); err != nil {
		log.Fatal().Err(err).Msg("write the icon")
	}
}
