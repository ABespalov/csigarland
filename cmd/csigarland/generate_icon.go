// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Makes app.ico and app.icns: a pixel cG, in the wordmark's letters and colours, on the window's
// slate, square for Windows and on a rounded square of Apple's grid for macOS.
// Contributors: None

//go:build ignore

// From cmd/csigarland, and then the resource that puts it in the program, with csigarland.manifest:
//
//	go run generate_icon.go
//	go run github.com/akavel/rsrc@v0.10.2 -arch amd64 -ico app.ico -manifest csigarland.manifest -o rsrc_windows_amd64.syso
//
// app.icns needs nothing more: deploy/build-utility.sh puts it in the bundle on macOS.
package main

import (
	"bytes"
	"encoding/binary"
	"image"
	"image/png"
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

// On Apple's grid an icon of 1024 is a rounded square of 824, 100 in from each edge, its corners of 185.
const (
	appleGrid   = 1024
	appleInset  = 100
	appleCorner = 185
)

// macOS's icons: the type of each PNG in an .icns, and its size in pixels.
var appleSizes = []struct {
	kind string
	size int
}{
	{"icp4", 16}, {"icp5", 32}, {"icp6", 64}, {"ic07", 128}, {"ic08", 256}, {"ic09", 512}, {"ic10", 1024},
	{"ic11", 32}, {"ic12", 64}, {"ic13", 256}, {"ic14", 512},
}

func paint(size int) []byte { return paintOn(size, 0, 0) }

// paintOn draws the letters on a square of slate inset from each edge, its corners cut round, and leaves
// what is outside it clear.
func paintOn(size, inset, corner int) []byte {
	pixels := make([]byte, size*size*bytesPerRGBA)
	body := size - 2*inset
	for y := range size {
		for x := range size {
			if insideRounded(x-inset, y-inset, body, corner) {
				copy(pixels[(y*size+x)*bytesPerRGBA:], slate[:])
			}
		}
	}
	scale := max(body/gridFor, 1)
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

// insideRounded says whether the pixel at x, y is in a square of side body whose corners are cut round.
func insideRounded(x, y, body, corner int) bool {
	if x < 0 || y < 0 || x >= body || y >= body {
		return false
	}
	// From the middle of the pixel to the middle of the nearest corner's circle, in halves of a pixel.
	nearest := func(at int) int {
		switch {
		case at < corner:
			return 2*corner - (2*at + 1)
		case at >= body-corner:
			return 2*at + 1 - 2*(body-corner)
		}
		return 0
	}
	dx, dy := nearest(x), nearest(y)
	return dx == 0 || dy == 0 || dx*dx+dy*dy <= 4*corner*corner
}

// applePNG is the icon of macOS at a size, as a PNG.
func applePNG(size int) []byte {
	pixels := paintOn(size, size*appleInset/appleGrid, size*appleCorner/appleGrid)
	picture := image.NewNRGBA(image.Rect(0, 0, size, size))
	for i := 0; i < len(pixels); i += bytesPerRGBA {
		// Blue, green, red and alpha as an icon has them, and red first as a PNG does.
		copy(picture.Pix[i:], []byte{pixels[i+2], pixels[i+1], pixels[i], pixels[i+3]})
	}
	var out bytes.Buffer
	if err := png.Encode(&out, picture); err != nil {
		log.Fatal().Err(err).Msg("encode the icon of macOS")
	}
	return out.Bytes()
}

// icns is the icon file of macOS: its type and length, then each image by its type and length.
func icns() []byte {
	var images []byte
	for _, entry := range appleSizes {
		data := applePNG(entry.size)
		header := make([]byte, 8)
		copy(header, entry.kind)
		binary.BigEndian.PutUint32(header[4:], uint32(len(header)+len(data)))
		images = append(append(images, header...), data...)
	}
	out := make([]byte, 8, 8+len(images))
	copy(out, "icns")
	binary.BigEndian.PutUint32(out[4:], uint32(8+len(images)))
	return append(out, images...)
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
	if err := os.WriteFile("app.icns", icns(), 0o644); err != nil {
		log.Fatal().Err(err).Msg("write the icon of macOS")
	}
}
