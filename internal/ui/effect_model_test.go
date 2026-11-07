// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tune's model of a file: each field read, and each change one field's bytes and no more.
// Contributors: None

package ui

import (
	"strings"
	"testing"
	"time"

	"gioui.org/io/input"
	"gioui.org/op"

	"github.com/ABespalov/csigarland/internal/engine"
)

// The owner's own layout, which every change must leave as it was but for what it changes.
const laidOut = "{\n  \"format\": \"csigarland.effect/1\",\n  \"name\": \"Mine\",\n  \"layers\": [\n" +
	"    { \"fx\": \"fire\",  \"sx\":  96, \"rev\": true,\n      \"pal\": [0, \"000000\", 255, [255, 64, 0]] },\n" +
	"    {\"fx\": \"sparkle\", \"col\": [\"FFFFFF\", \"0A2A4A\"], \"blend\": \"add\"}\n  ]\n}\n"

func readModel(t *testing.T, file string) *EffectModel {
	t.Helper()
	model, err := ReadEffect([]byte(file))
	if err != nil {
		t.Fatalf("%v\n%s", err, file)
	}
	return model
}

// What the controls write, the firmware's parser takes.
func mustParse(t *testing.T, file []byte) {
	t.Helper()
	if err := engine.Check(file); err != nil {
		t.Fatalf("%v\n%s", err, file)
	}
}

func TestTheModelReadsEveryField(t *testing.T) {
	model := readModel(t, laidOut)
	if model.Layers() != 2 || model.Name() != "Mine" {
		t.Fatalf("%d layers, %q", model.Layers(), model.Name())
	}
	if model.String(0, "fx", "") != "fire" || model.Int(0, "sx", 128) != 96 || model.Int(0, "ix", 128) != 128 ||
		!model.Bool(0, "rev") || model.Bool(1, "rev") || model.String(1, "blend", "replace") != "add" {
		t.Fatal("a field was read wrong")
	}
	for _, bad := range []string{"{", "[]", `{"layers": 1}`, `{"layers": [1]}`} {
		if _, err := ReadEffect([]byte(bad)); err == nil {
			t.Errorf("%s was read", bad)
		}
	}
}

// A key written twice is the first one to the firmware's parser, so it is the one a control shows and
// changes: the other changes nothing the preview or the garland plays. The rest of the file stays as it was.
func TestAKeyWrittenTwiceIsTheFirstAsTheFirmwareTakesIt(t *testing.T) {
	const twice = `{"format":"csigarland.effect/1","name":"W","layers":[{"fx":"solid","col":["FFFFFF"],"bri":0,"bri":255}]}`
	model := readModel(t, twice)
	if model.Int(0, "bri", 128) != 0 {
		t.Fatalf("brightness read as %d", model.Int(0, "bri", 128))
	}
	changed := model.SetField(0, "bri", 128)
	if want := strings.Replace(twice, `"bri":0`, `"bri":128`, 1); string(changed) != want {
		t.Fatalf("%s", changed)
	}
	lit := func(file []byte) engine.Color {
		t.Helper()
		preview, err := engine.NewPreview(file)
		if err != nil {
			t.Fatal(err)
		}
		pixels := make([]engine.Color, 1)
		preview.Frame(0, 100, pixels)
		return pixels[0]
	}
	if dark, shown := lit([]byte(twice)), lit(changed); dark != (engine.Color{}) || shown == (engine.Color{}) {
		t.Fatalf("the preview went %v, then %v", dark, shown)
	}
	// The colours too: those of a col or pal written again are none the effect plays, and no swatch of theirs
	// shares the first's place, as the picker would then edit the first while showing the second.
	colors, err := FileColors([]byte(`{"format":"csigarland.effect/1","name":"W","layers":[{"fx":"solid","col":["FF0000"],"col":["0000FF"],"pal":[0,"00FF00",255,"00FF00"],"pal":[0,"FFFFFF"]}]}`))
	if err != nil {
		t.Fatal(err)
	}
	if len(colors) != 3 || colors[0].Color != (RGB{R: 255}) || colors[0].Field != "col" || colors[1].Field != "pal" || colors[2].Field != "pal" || colors[1].Color != (RGB{G: 255}) {
		t.Fatalf("%+v", colors)
	}
	for i := range colors {
		for j := i + 1; j < len(colors); j++ {
			if colors[i].Same(colors[j]) {
				t.Fatalf("%+v and %+v share a place", colors[i], colors[j])
			}
		}
	}
	// And a second list of layers is no layer of the effect, as it is none to the parser.
	const layersTwice = `{"format":"csigarland.effect/1","name":"W","layers":[{"fx":"solid"}],"layers":[{"fx":"fire"},{"fx":"fire"}]}`
	if two := readModel(t, layersTwice); two.Layers() != 1 || two.String(0, "fx", "") != "solid" {
		t.Fatalf("%d layers, the first %q", two.Layers(), two.String(0, "fx", ""))
	}
}

func TestAControlChangesItsFieldAndNoOtherByte(t *testing.T) {
	model := readModel(t, laidOut)
	// In place: the value alone.
	if got := string(model.SetField(0, "sx", 200)); got != strings.Replace(laidOut, "96", "200", 1) {
		t.Fatalf("%s", got)
	}
	// Not there yet: first in its layer, the rest as it was.
	added := model.SetField(1, "ix", 7)
	if !strings.Contains(string(added), `{"ix": 7, "fx": "sparkle"`) || strings.Replace(string(added), `"ix": 7, `, "", 1) != laidOut {
		t.Fatalf("%s", added)
	}
	mustParse(t, added)
	// Out, with its comma, wherever it stands.
	for key, layer := range map[string]int{"fx": 1, "col": 1, "blend": 1, "rev": 0, "pal": 0} {
		out := model.DeleteField(layer, key)
		after := readModel(t, string(out))
		if after.Raw(layer, key) != nil || after.Layers() != 2 {
			t.Errorf("%s stayed: %s", key, out)
		}
		if key != "fx" {
			mustParse(t, out)
		}
	}
	lone := readModel(t, `{"format":"csigarland.effect/1","name":"N","layers":[{"fx":"solid"}]}`)
	if got := string(lone.DeleteField(0, "fx")); got != `{"format":"csigarland.effect/1","name":"N","layers":[{}]}` {
		t.Errorf("%s", got)
	}
	if got := string(lone.SetField(0, "zone", []string{"star", "bell"})); !strings.Contains(got, `{"zone": ["star","bell"], "fx":"solid"}`) {
		t.Errorf("%s", got)
	}
}

func TestLayersAndTheNameAreChangedInPlace(t *testing.T) {
	model := readModel(t, laidOut)
	more := model.AddLayer()
	mustParse(t, more)
	if after := readModel(t, string(more)); after.Layers() != 3 || after.String(2, "fx", "") != defaultFx {
		t.Fatalf("%s", more)
	}
	for layer := range 2 {
		fewer := model.RemoveLayer(layer)
		mustParse(t, fewer)
		if after := readModel(t, string(fewer)); after.Layers() != 1 || after.String(0, "fx", "") != []string{"sparkle", "fire"}[layer] {
			t.Fatalf("taking layer %d left %s", layer, fewer)
		}
	}
	lone := readModel(t, string(model.RemoveLayer(0)))
	if got := string(lone.RemoveLayer(0)); readModel(t, got).Layers() != 1 {
		t.Fatal("the last layer went")
	}
	full := readModel(t, string(readModel(t, string(more)).AddLayer()))
	if full.Layers() != maxLayers || string(full.AddLayer()) != string(full.file) {
		t.Fatal("a fifth layer was added")
	}
	renamed := model.SetName(`Ours "new"`)
	if got := readModel(t, string(renamed)).Name(); got != `Ours "new"` || !strings.Contains(string(renamed), `"name": "Ours \"new\""`) {
		t.Fatalf("%q in %s", got, renamed)
	}
	if validEffectName(`Ours "new"`) || !validEffectName("Ours, new") || validEffectName("") {
		t.Fatal("names are judged as the parser does")
	}
}

func TestEveryColourIsFoundAndChangedInItsOwnForm(t *testing.T) {
	colors, err := FileColors([]byte(laidOut))
	if err != nil {
		t.Fatal(err)
	}
	if len(colors) != 4 {
		t.Fatalf("%+v", colors)
	}
	stop, last := colors[1], colors[3]
	if stop.Field != "pal" || stop.Stop != 255 || stop.Color != (RGB{255, 64, 0}) || !stop.array ||
		last.Field != "col" || last.Layer != 1 || last.Index != 1 || last.Color.Hex() != "0A2A4A" {
		t.Fatalf("%+v", colors)
	}
	changed := SetFileColor([]byte(laidOut), stop, RGB{1, 2, 3})
	if string(changed) != strings.Replace(laidOut, "[255, 64, 0]", "[1, 2, 3]", 1) {
		t.Fatalf("%s", changed)
	}
	changed = SetFileColor([]byte(laidOut), last, RGB{0xAB, 0xCD, 0xEF})
	if string(changed) != strings.Replace(laidOut, "0A2A4A", "ABCDEF", 1) {
		t.Fatalf("%s", changed)
	}
	named, err := FileColors([]byte(`{"layers":[{"fx":"rainbow","pal":"Rainbow","col":"FF0000"}]}`))
	if err != nil || len(named) != 1 || named[0].Field != "col" {
		t.Fatalf("%+v %v", named, err)
	}
}

func TestAColourGoesToHueAndBack(t *testing.T) {
	for _, c := range []RGB{{0, 0, 0}, {255, 255, 255}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {0x10, 0x38, 0x50}, {0x60, 0xD0, 0xFF}, {200, 100, 50}} {
		if back := c.ToHSV().ToRGB(); back != c {
			t.Errorf("%v came back as %v", c, back)
		}
	}
	if hue := (RGB{0, 255, 255}).ToHSV().H; hue < 179 || hue > 181 {
		t.Errorf("cyan's hue is %v", hue)
	}
	if c, ok := ParseHex("#60d0ff"); !ok || c != (RGB{0x60, 0xD0, 0xFF}) {
		t.Errorf("%v %v", c, ok)
	}
	for _, bad := range []string{"", "12345", "GGGGGG", "1234567"} {
		if _, ok := ParseHex(bad); ok {
			t.Errorf("%q was read", bad)
		}
	}
}

// The controls show what the file says, as soon as it says it, and write the file when moved.
func TestTheControlsAndTheFileFollowEachOther(t *testing.T) {
	a := sceneTune(t)
	frame := func() {
		var ops op.Ops
		var router input.Router
		gtx := sceneContext(&ops, &router, sceneWidth, sceneHeight)
		gtx.Now = time.Now()
		a.Frame(gtx)
	}
	a.tune.setFile(laidOut)
	frame()
	controls := &a.tune.layers[0]
	if !controls.rev.Value || controls.sx.Value != 96.0/255 || controls.palette.Value != "gradient" || a.tune.layers[1].blend.Value != "add" {
		t.Fatalf("rev %v, sx %v, palette %q, blend %q", controls.rev.Value, controls.sx.Value, controls.palette.Value, a.tune.layers[1].blend.Value)
	}
	if a.tune.name.Text() != "Mine" {
		t.Fatalf("the name shows %q", a.tune.name.Text())
	}
	a.tune.write(a.tune.model.SetField(0, "bri", 40))
	frame()
	if controls.bri.Value != 40.0/255 || !strings.Contains(a.tune.file.Text(), `"bri": 40`) {
		t.Fatalf("bri %v in %s", controls.bri.Value, a.tune.file.Text())
	}
}
