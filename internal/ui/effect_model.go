// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: An effect's file as the controls of Tune see it: each field of each layer, and where it is.
// Contributors: None

package ui

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
)

// The firmware's limits, as effect_spec.h and effect_parser.cpp have them.
const (
	maxLayers       = 4
	maxLayerColors  = 3
	maxPaletteStops = 16
	minPaletteStops = 2
	defaultFx       = "solid"
)

// span is where a value stands in the file's bytes.
type span struct{ start, end int }

// field is where a field stands: from its key, as written, which may spell a letter as an escape,
// and its value.
type field struct {
	key   int // Where its key's opening quote is.
	value span
}

// layerPlace is one layer: where its object is, and each field in it.
type layerPlace struct {
	whole  span // From its { to just after its }.
	fields map[string]field
}

// EffectModel is a file as its controls read and write it. The file stays the owner's to lay out: a
// change writes the bytes of one value, or of one field, and leaves every other byte as it was.
type EffectModel struct {
	file    []byte
	top     map[string]span
	topOpen int // Just inside the file's {.
	layers  []layerPlace
}

// errEscapedNul is a file with \u0000 in a key or a text, which the firmware's parser refuses: cJSON ends the
// string there, so "bri\u0000x" would be a second "bri" to the garland, and another key to the controls here,
// whose slider would show and change what nothing plays.
var errEscapedNul = errors.New(`a key or text holds \u0000, which the garland refuses: take it out`)

// holdsNul is whether a string of the file, a key or a value, holds a NUL once decoded. A file that is not JSON
// says nothing here, and is refused for that.
func holdsNul(file []byte) bool {
	decoder := json.NewDecoder(bytes.NewReader(file))
	for {
		token, err := decoder.Token()
		if err != nil {
			return false
		}
		if text, ok := token.(string); ok && strings.ContainsRune(text, 0) {
			return true
		}
	}
}

// ReadEffect places every field of a file. A file that is not JSON in this shape has no model, and its
// controls wait until it is; so does one the firmware refuses for a NUL in a key or a text, see errEscapedNul.
func ReadEffect(file []byte) (*EffectModel, error) {
	if holdsNul(file) {
		return nil, errEscapedNul
	}
	model := &EffectModel{file: file, top: map[string]span{}}
	decoder := json.NewDecoder(bytes.NewReader(file))
	if err := expectDelim(decoder, '{'); err != nil {
		return nil, err
	}
	model.topOpen = int(decoder.InputOffset())
	hasLayers := false
	for decoder.More() {
		key, err := decoder.Token()
		if err != nil {
			return nil, fmt.Errorf("the file is not JSON: %w", err)
		}
		name, _ := key.(string)
		// The first of a key written twice, layers among them, as the firmware's parser takes it.
		if name != "layers" || hasLayers {
			value, err := rawSpan(decoder)
			if err != nil {
				return nil, err
			}
			if _, seen := model.top[name]; !seen && name != "layers" {
				model.top[name] = value
			}
			continue
		}
		if err := model.readLayers(decoder); err != nil {
			return nil, err
		}
		hasLayers = true
	}
	if err := expectDelim(decoder, '}'); err != nil {
		return nil, err
	}
	if !hasLayers {
		return nil, errors.New("the file has no layers")
	}
	return model, nil
}

func (m *EffectModel) readLayers(decoder *json.Decoder) error {
	if err := expectDelim(decoder, '['); err != nil {
		return err
	}
	for decoder.More() {
		if err := expectDelim(decoder, '{'); err != nil {
			return errors.New("a layer is not an object")
		}
		layer := layerPlace{whole: span{start: int(decoder.InputOffset()) - 1}, fields: map[string]field{}}
		for decoder.More() {
			// Only whitespace and a comma come between the last value and the key's quote.
			keyAt := bytes.IndexByte(m.file[decoder.InputOffset():], '"') + int(decoder.InputOffset())
			key, err := decoder.Token()
			if err != nil {
				return fmt.Errorf("the file is not JSON: %w", err)
			}
			value, err := rawSpan(decoder)
			if err != nil {
				return err
			}
			name, _ := key.(string)
			// The first of a key written twice, as the firmware's parser takes it: a control of the other would
			// change what nothing plays.
			if _, seen := layer.fields[name]; !seen {
				layer.fields[name] = field{key: keyAt, value: value}
			}
		}
		if err := expectDelim(decoder, '}'); err != nil {
			return err
		}
		layer.whole.end = int(decoder.InputOffset())
		m.layers = append(m.layers, layer)
	}
	return expectDelim(decoder, ']')
}

// The next value, and where its bytes are.
func rawSpan(decoder *json.Decoder) (span, error) {
	var raw json.RawMessage
	if err := decoder.Decode(&raw); err != nil {
		return span{}, fmt.Errorf("the file is not JSON: %w", err)
	}
	end := int(decoder.InputOffset())
	return span{start: end - len(raw), end: end}, nil
}

// firstMembers is an object's members as the firmware's parser finds them: by the exact key, case and all,
// the first of a key written twice. Nil when raw is no object.
func firstMembers(raw json.RawMessage) map[string]json.RawMessage {
	decoder := json.NewDecoder(bytes.NewReader(raw))
	if expectDelim(decoder, '{') != nil {
		return nil
	}
	members := map[string]json.RawMessage{}
	for decoder.More() {
		key, err := decoder.Token()
		if err != nil {
			return nil
		}
		var value json.RawMessage
		if decoder.Decode(&value) != nil {
			return nil
		}
		if name, _ := key.(string); members[name] == nil {
			members[name] = value
		}
	}
	return members
}

// wholeNumber is a member's whole number, or fallback when it is not there or no whole number.
func wholeNumber(members map[string]json.RawMessage, key string, fallback int) int {
	var value float64
	if raw := members[key]; raw != nil && json.Unmarshal(raw, &value) == nil && value == float64(int(value)) {
		return int(value)
	}
	return fallback
}

// zoneEvery is a zone of every n-th LED from an offset, 0 for a member left out.
type zoneEvery struct{ Every, Offset int }

// readZoneEvery reads a zone's object as the firmware's parse_zone does, so that a control shows the period
// and offset that play, and a change of one writes back the other as it plays.
func readZoneEvery(raw json.RawMessage) zoneEvery {
	members := firstMembers(raw)
	return zoneEvery{Every: wholeNumber(members, "every", 0), Offset: wholeNumber(members, "offset", 0)}
}

// Layers is how many there are.
func (m *EffectModel) Layers() int { return len(m.layers) }

// Raw is a layer's field as written, or nil when it is not there.
func (m *EffectModel) Raw(layer int, key string) json.RawMessage {
	if layer < 0 || layer >= len(m.layers) {
		return nil
	}
	if at, known := m.layers[layer].fields[key]; known {
		return json.RawMessage(m.file[at.value.start:at.value.end])
	}
	return nil
}

// Int is a layer's number, or fallback when it is not there or not a whole number.
func (m *EffectModel) Int(layer int, key string, fallback int) int {
	var value float64
	if raw := m.Raw(layer, key); raw != nil && json.Unmarshal(raw, &value) == nil && value == float64(int(value)) {
		return int(value)
	}
	return fallback
}

// Bool is a layer's flag, false when it is not there.
func (m *EffectModel) Bool(layer int, key string) bool {
	var value bool
	if raw := m.Raw(layer, key); raw != nil && json.Unmarshal(raw, &value) == nil {
		return value
	}
	return false
}

// String is a layer's word, or fallback.
func (m *EffectModel) String(layer int, key, fallback string) string {
	var value string
	if raw := m.Raw(layer, key); raw != nil && json.Unmarshal(raw, &value) == nil {
		return value
	}
	return fallback
}

// Name is the effect's name.
func (m *EffectModel) Name() string {
	var name string
	if at, known := m.top["name"]; known {
		_ = json.Unmarshal(m.file[at.start:at.end], &name)
	}
	return name
}

// jsonText writes a value as short as JSON allows, with nothing escaped that need not be.
func jsonText(value any) string {
	data, err := marshalShort(value)
	if err != nil {
		return "null"
	}
	return string(data)
}

func marshalShort(value any) ([]byte, error) {
	var out bytes.Buffer
	encoder := json.NewEncoder(&out)
	encoder.SetEscapeHTML(false)
	if err := encoder.Encode(value); err != nil {
		return nil, err
	}
	return bytes.TrimSpace(out.Bytes()), nil
}

// SetField writes a layer's field: in place of its value if it is there, or first in the layer.
func (m *EffectModel) SetField(layer int, key string, value any) []byte {
	if layer < 0 || layer >= len(m.layers) {
		return m.file
	}
	words := jsonText(value)
	place := m.layers[layer]
	if at, known := place.fields[key]; known {
		return splice(m.file, at.value.start, at.value.end, words)
	}
	field := jsonText(key) + ": " + words
	if len(place.fields) > 0 {
		field += ", "
	}
	return splice(m.file, place.whole.start+1, place.whole.start+1, field)
}

// DeleteField takes a layer's field out, with the comma that went with it.
func (m *EffectModel) DeleteField(layer int, key string) []byte {
	if layer < 0 || layer >= len(m.layers) {
		return m.file
	}
	// Every copy of a key written twice, each taken out as the first, as the parser reads that one: one left
	// would be the one it reads then, and the field would stay set.
	model := m
	for {
		at, known := model.layers[layer].fields[key]
		if !known || at.key < 0 {
			return model.file
		}
		next, err := ReadEffect(model.cut(at.key, at.value.end))
		if err != nil || len(next.layers) != len(m.layers) {
			return model.cut(at.key, at.value.end)
		}
		model = next
	}
}

// cut takes out an item of a list or an object, and the comma between it and its neighbour.
func (m *EffectModel) cut(start, end int) []byte {
	after := end
	for after < len(m.file) && isSpace(m.file[after]) {
		after++
	}
	if after < len(m.file) && m.file[after] == ',' {
		after++
		for after < len(m.file) && isSpace(m.file[after]) {
			after++
		}
		return splice(m.file, start, after, "")
	}
	before := start
	for before > 0 && isSpace(m.file[before-1]) {
		before--
	}
	if before > 0 && m.file[before-1] == ',' {
		return splice(m.file, before-1, end, "")
	}
	return splice(m.file, start, end, "")
}

func isSpace(b byte) bool { return b == ' ' || b == '\t' || b == '\n' || b == '\r' }

// AddLayer puts a new layer at the end, which draws over the others.
func (m *EffectModel) AddLayer() []byte {
	if len(m.layers) == 0 || len(m.layers) >= maxLayers {
		return m.file
	}
	last := m.layers[len(m.layers)-1].whole.end
	return splice(m.file, last, last, `, {"fx": "`+defaultFx+`"}`)
}

// RemoveLayer takes a layer out. The last one stays: an effect has at least one.
func (m *EffectModel) RemoveLayer(layer int) []byte {
	if layer < 0 || layer >= len(m.layers) || len(m.layers) == 1 {
		return m.file
	}
	return m.cut(m.layers[layer].whole.start, m.layers[layer].whole.end)
}

// SetName writes the effect's name in place of the one there.
func (m *EffectModel) SetName(name string) []byte {
	if at, known := m.top["name"]; known {
		return splice(m.file, at.start, at.end, jsonText(name))
	}
	return splice(m.file, m.topOpen, m.topOpen, `"name": `+jsonText(name)+", ")
}
