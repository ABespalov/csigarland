// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The colours of an effect's file, where each one stands in it, and changing one alone.
// Contributors: None

package ui

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"strings"
)

// RGB is a colour as an effect's file has it.
type RGB struct{ R, G, B uint8 }

// Hex is the colour as the file writes it: FF4000.
func (c RGB) Hex() string { return fmt.Sprintf("%02X%02X%02X", c.R, c.G, c.B) }

// ParseHex reads RRGGBB, with or without a # before it.
func ParseHex(text string) (RGB, bool) {
	text = strings.TrimPrefix(strings.TrimSpace(text), "#")
	raw, err := hex.DecodeString(text)
	if err != nil || len(raw) != 3 {
		return RGB{}, false
	}
	return RGB{raw[0], raw[1], raw[2]}, true
}

// FileColor is one colour of an effect's file: of which layer, in col or as a stop of pal, and where
// its bytes are, for it to be changed alone.
type FileColor struct {
	Layer int
	Field string // "col" or "pal".
	Index int    // Its place among the colours of that field.
	Stop  int    // A palette stop's place, 0 to 255; -1 in col.
	Color RGB
	start int
	end   int
	array bool // Written [r, g, b] rather than "RRGGBB", and to be written so again.
}

// Same says whether two are the same place in a file, whatever their colour now.
func (c FileColor) Same(other FileColor) bool {
	return c.Layer == other.Layer && c.Field == other.Field && c.Index == other.Index
}

// FileColors lists every colour of every layer, in the order of the file: col's, then pal's stops,
// as the Effects Guide has them. A palette by name has no colours of its own here.
func FileColors(file []byte) ([]FileColor, error) {
	decoder := json.NewDecoder(bytes.NewReader(file))
	if err := expectDelim(decoder, '{'); err != nil {
		return nil, err
	}
	for decoder.More() {
		key, err := decoder.Token()
		if err != nil {
			return nil, fmt.Errorf("the file is not JSON: %w", err)
		}
		if key != "layers" {
			if err := skipValue(decoder); err != nil {
				return nil, err
			}
			continue
		}
		return layerColors(decoder)
	}
	return nil, nil
}

func layerColors(decoder *json.Decoder) ([]FileColor, error) {
	if err := expectDelim(decoder, '['); err != nil {
		return nil, err
	}
	var found []FileColor
	for layer := 0; decoder.More(); layer++ {
		if err := expectDelim(decoder, '{'); err != nil {
			return nil, err
		}
		// The first of a key written twice, as the firmware's parser and the effect model take it: the colours
		// of the other are none the effect plays, and would share the first's places.
		seen := map[string]bool{}
		for decoder.More() {
			key, err := decoder.Token()
			if err != nil {
				return nil, fmt.Errorf("the file is not JSON: %w", err)
			}
			name, _ := key.(string)
			var colors []FileColor
			switch {
			case (name == "col" || name == "pal") && !seen[name]:
				colors, err = fieldColors(decoder, layer, name)
			default:
				err = skipValue(decoder)
			}
			seen[name] = true
			if err != nil {
				return nil, err
			}
			found = append(found, colors...)
		}
		if err := expectDelim(decoder, '}'); err != nil {
			return nil, err
		}
	}
	return found, nil
}

// The colours of one col or pal, at what offsets. A pal alternates a stop's place and its colour.
func fieldColors(decoder *json.Decoder, layer int, field string) ([]FileColor, error) {
	var whole json.RawMessage
	if err := decoder.Decode(&whole); err != nil {
		return nil, fmt.Errorf("the file is not JSON: %w", err)
	}
	end := int(decoder.InputOffset())
	start := end - len(whole)
	// One colour on its own, as a string: a list of three numbers is a list, not a colour.
	if c, isColor := readColor(whole); isColor && field == "col" && whole[0] == '"' {
		return []FileColor{{Layer: layer, Field: field, Stop: -1, Color: c.Color, start: start, end: end}}, nil
	}
	var items []json.RawMessage
	if json.Unmarshal(whole, &items) != nil {
		return nil, nil // A palette by its name.
	}
	// Each item's offset, found in order within the field's own bytes.
	var found []FileColor
	at := 1
	stop := -1
	for i, item := range items {
		offset := bytes.Index(whole[at:], item)
		if offset < 0 {
			return nil, errors.New("the file's colours could not be placed")
		}
		itemStart := start + at + offset
		at += offset + len(item)
		if field == "pal" && i%2 == 0 {
			var place float64
			if json.Unmarshal(item, &place) == nil {
				stop = int(place)
			}
			continue
		}
		c, isColor := readColor(item)
		if !isColor {
			continue
		}
		color := FileColor{Layer: layer, Field: field, Index: len(found), Stop: -1, Color: c.Color,
			start: itemStart, end: itemStart + len(item), array: c.array}
		if field == "pal" {
			color.Stop = stop
		}
		found = append(found, color)
	}
	return found, nil
}

// A colour as "RRGGBB" or as [r, g, b].
func readColor(raw json.RawMessage) (FileColor, bool) {
	var text string
	if json.Unmarshal(raw, &text) == nil {
		c, ok := ParseHex(text)
		return FileColor{Color: c}, ok
	}
	var parts []int
	if json.Unmarshal(raw, &parts) == nil && len(parts) == 3 {
		for _, part := range parts {
			if part < 0 || part > 255 {
				return FileColor{}, false
			}
		}
		return FileColor{Color: RGB{uint8(parts[0]), uint8(parts[1]), uint8(parts[2])}, array: true}, true
	}
	return FileColor{}, false
}

// SetFileColor writes one colour in place of what was there, in the same form, and not one other byte
// of the file: the file is the owner's to lay out.
func SetFileColor(file []byte, at FileColor, c RGB) []byte {
	words := `"` + c.Hex() + `"`
	if at.array {
		words = fmt.Sprintf("[%d, %d, %d]", c.R, c.G, c.B)
	}
	return splice(file, at.start, at.end, words)
}

// HSV is a colour as a picker moves through it: hue 0 to 360, saturation and value 0 to 1.
type HSV struct{ H, S, V float64 }

// ToHSV is a colour's hue, saturation and value.
func (c RGB) ToHSV() HSV {
	r, g, b := float64(c.R)/255, float64(c.G)/255, float64(c.B)/255
	high, low := math.Max(r, math.Max(g, b)), math.Min(r, math.Min(g, b))
	spread := high - low
	out := HSV{V: high}
	if high > 0 {
		out.S = spread / high
	}
	switch {
	case spread == 0:
	case high == r:
		out.H = 60 * math.Mod((g-b)/spread, 6)
	case high == g:
		out.H = 60 * ((b-r)/spread + 2)
	default:
		out.H = 60 * ((r-g)/spread + 4)
	}
	if out.H < 0 {
		out.H += 360
	}
	return out
}

// ToRGB is the colour of a hue, saturation and value.
func (h HSV) ToRGB() RGB {
	hue := math.Mod(h.H, 360) / 60
	chroma := h.V * h.S
	x := chroma * (1 - math.Abs(math.Mod(hue, 2)-1))
	var r, g, b float64
	switch int(hue) {
	case 0:
		r, g = chroma, x
	case 1:
		r, g = x, chroma
	case 2:
		g, b = chroma, x
	case 3:
		g, b = x, chroma
	case 4:
		r, b = x, chroma
	default:
		r, b = chroma, x
	}
	low := h.V - chroma
	channel := func(v float64) uint8 { return uint8(math.Round(math.Max(0, math.Min(1, v+low)) * 255)) }
	return RGB{channel(r), channel(g), channel(b)}
}
