// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests that every shipped effect runs through the firmware's engine from Go.
// Contributors: None

package engine

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const effectsFolder = "../../firmware/app/effects"

func shippedEffects(t *testing.T) []string {
	t.Helper()
	paths, err := filepath.Glob(filepath.Join(effectsFolder, "*.json"))
	if err != nil {
		t.Fatal(err)
	}
	var effects []string
	for _, path := range paths {
		name := filepath.Base(path)
		if name != "catalog.json" && name != "carousel.json" && !strings.Contains(name, "schema") {
			effects = append(effects, path)
		}
	}
	return effects
}

func TestAnEffectIsDrawnAndMoves(t *testing.T) {
	preview, err := NewPreview([]byte(`{"format":"csigarland.effect/1","name":"T","layers":[{"fx":"chase","col":["FF0000"]}]}`))
	if err != nil {
		t.Fatal(err)
	}
	first := make([]Color, 32)
	second := make([]Color, 32)
	if preview.Frame(0, 100, first) != 32 || preview.Frame(500, 100, second) != 32 {
		t.Fatal("not all LEDs were drawn")
	}
	lit := false
	different := false
	for i := range first {
		lit = lit || first[i] != Color{} || second[i] != Color{}
		different = different || first[i] != second[i]
	}
	if !lit || !different {
		t.Errorf("lit %v, moved %v", lit, different)
	}
}

func TestCheckingAFileLeavesAPreviewAsItIs(t *testing.T) {
	red, err := NewPreview([]byte(`{"format":"csigarland.effect/1","name":"R","layers":[{"fx":"solid","col":["FF0000"]}]}`))
	if err != nil {
		t.Fatal(err)
	}
	frame := make([]Color, 1)
	red.Frame(0, 100, frame)
	if frame[0].R == 0 || frame[0].B != 0 {
		t.Fatalf("%+v", frame[0])
	}
	if err := Check([]byte(`{"format":"csigarland.effect/1","name":"B","layers":[{"fx":"solid","col":["0000FF"]}]}`)); err != nil {
		t.Fatal(err)
	}
	red.Frame(100, 100, frame)
	if frame[0].R == 0 || frame[0].B != 0 {
		t.Fatalf("the preview drew what was checked: %+v", frame[0])
	}
}

func TestAFileWithANulByteIsRefused(t *testing.T) {
	if err := Check([]byte("{\"format\":\"csigarland.effect/1\"}\x00")); err == nil || !strings.Contains(err.Error(), "NUL") {
		t.Fatalf("%v", err)
	}
}

func TestTheFirmwaresParserSaysWhatIsWrong(t *testing.T) {
	for name, file := range map[string]string{
		"not JSON":          `{`,
		"unknown algorithm": `{"format":"csigarland.effect/1","name":"T","layers":[{"fx":"nonsense"}]}`,
		"no layers":         `{"format":"csigarland.effect/1","name":"T","layers":[]}`,
	} {
		err := Check([]byte(file))
		if err == nil || err.Error() == "" {
			t.Errorf("%s: %v", name, err)
		}
	}
}

func TestEveryShippedEffectRuns(t *testing.T) {
	effects := shippedEffects(t)
	if len(effects) < 100 {
		t.Fatalf("only %d effects found", len(effects))
	}
	frame := make([]Color, 32)
	for _, path := range effects {
		data, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		preview, err := NewPreview(data)
		if err != nil {
			t.Errorf("%s: %v", path, err)
			continue
		}
		lit := false
		for ms := uint32(0); ms < 40000 && !lit; ms += 97 {
			preview.Frame(ms, 100, frame)
			for _, color := range frame {
				lit = lit || color != Color{}
			}
		}
		if !lit {
			t.Errorf("%s stayed dark", path)
		}
	}
}

// The brightness is the garland's, on its curve, and the screen's own gamma takes the curve back: a
// percent looks about that share of full, and a garland at 15% is no dark preview.
func TestBrightnessIsAppliedAsTheGarlandAppliesIt(t *testing.T) {
	white, err := NewPreview([]byte(`{"format":"csigarland.effect/1","name":"W","layers":[{"fx":"solid","col":["FFFFFF"]}]}`))
	if err != nil {
		t.Fatal(err)
	}
	level := func(percent int) uint8 {
		frame := make([]Color, 1)
		white.Frame(0, percent, frame)
		return frame[0].R
	}
	full, half, low, least := int(level(100)), int(level(50)), int(level(15)), int(level(1))
	if full != 255 || half < 115 || half > 140 || low < 25 || low > 50 || least == 0 || least >= low {
		t.Fatalf("100%%: %d, 50%%: %d, 15%%: %d, 1%%: %d", full, half, low, least)
	}
}

// The parser's lists, which every shipped effect's fx is among.
func TestTheParsersNamesAreThere(t *testing.T) {
	algorithms, figurines := Algorithms(), Figurines()
	if len(algorithms) < 90 || algorithms[0] != "solid" || len(figurines) != 8 || figurines[0] != "tree" {
		t.Fatalf("%d algorithms from %q, figurines %v", len(algorithms), algorithms[0], figurines)
	}
	for _, name := range algorithms {
		file := `{"format":"csigarland.effect/1","name":"T","layers":[{"fx":"` + name + `"}]}`
		if err := Check([]byte(file)); err != nil {
			t.Errorf("%s: %v", name, err)
		}
	}
}

// One preview taking over from another must not draw the other's effect.
func TestTwoPreviewsTakeTurns(t *testing.T) {
	red, err := NewPreview([]byte(`{"format":"csigarland.effect/1","name":"R","layers":[{"fx":"solid","col":["FF0000"]}]}`))
	if err != nil {
		t.Fatal(err)
	}
	blue, err := NewPreview([]byte(`{"format":"csigarland.effect/1","name":"B","layers":[{"fx":"solid","col":["0000FF"]}]}`))
	if err != nil {
		t.Fatal(err)
	}
	frame := make([]Color, 4)
	blue.Frame(0, 100, frame)
	if frame[0].B == 0 || frame[0].R != 0 {
		t.Errorf("blue drew %v", frame[0])
	}
	red.Frame(0, 100, frame)
	if frame[0].R == 0 || frame[0].B != 0 {
		t.Errorf("red drew %v", frame[0])
	}
}

// A run draws a file apart from the previews, the same from its start each time, what is random in
// it too, and takes nothing of theirs: the one playing goes on with its own effect.
func TestARunIsApartFromThePreviews(t *testing.T) {
	blue, err := NewPreview([]byte(`{"format":"csigarland.effect/1","name":"B","layers":[{"fx":"solid","col":["0000FF"]}]}`))
	if err != nil {
		t.Fatal(err)
	}
	sparkle := []byte(`{"format":"csigarland.effect/1","name":"S","layers":[{"fx":"sparkle","ix":200,"col":["FF0000"]}]}`)
	times := []uint32{0, 400, 800}
	first, err := Run(sparkle, times, 8)
	if err != nil || len(first) != len(times)*8 {
		t.Fatalf("%d colours, %v", len(first), err)
	}
	again, _ := Run(sparkle, times, 8)
	for i := range first {
		if first[i] != again[i] {
			t.Fatalf("LED %d of the runs differs: %v and %v", i, first[i], again[i])
		}
	}
	frame := make([]Color, 4)
	blue.Frame(0, 100, frame)
	if frame[0].B == 0 || frame[0].R != 0 {
		t.Errorf("blue drew %v after a run", frame[0])
	}
	if _, err := Run([]byte(`{}`), times, 8); err == nil {
		t.Error("a wrong file ran")
	}
}

// The limit is the firmware's own, worked out from the power settings as it does: the defaults keep the
// 1400 mA of the releases before them, and settings it would refuse change nothing.
func TestTheLimitIsTheFirmwares(t *testing.T) {
	defaults := DefaultPower()
	t.Cleanup(func() { _ = SetPower(defaults) })
	if defaults != (Power{2000, 40, 1, 20}) || LimitMilliamps() != 1400 {
		t.Fatalf("defaults %+v, limit %d", defaults, LimitMilliamps())
	}
	if err := SetPower(Power{5000, 36, 1, 10}); err != nil || LimitMilliamps() != 4300 {
		t.Fatalf("%v, limit %d", err, LimitMilliamps())
	}
	if err := SetPower(Power{5000, 36, 1, 30}); err == nil || LimitMilliamps() != 4300 {
		t.Fatalf("a margin of 30%% taken: %v, limit %d", err, LimitMilliamps())
	}
}

// White on every LED at full brightness draws what the settings say, and less at a lower brightness, as
// the firmware's model says; the preview and a run say the same.
func TestTheCurrentIsTheFirmwaresModel(t *testing.T) {
	defaults := DefaultPower()
	t.Cleanup(func() { _ = SetPower(defaults) })
	white := []byte(`{"format":"csigarland.effect/1","name":"W","layers":[{"fx":"solid","col":["FFFFFF"]}]}`)
	full, err := Currents(white, []uint32{0, 1000}, 100, 32)
	if err != nil {
		t.Fatal(err)
	}
	half, err := Currents(white, []uint32{0}, 50, 32)
	if err != nil {
		t.Fatal(err)
	}
	// 32 LEDs at 40 + 1 mA.
	if full[0] != 1312 || half[0] >= full[0] || half[0] <= 0 || full[0] != full[1] {
		t.Fatalf("full %v, half %v", full, half)
	}
	if err := SetPower(Power{2000, 55, 1, 20}); err != nil {
		t.Fatal(err)
	}
	if past, _ := Currents(white, []uint32{0}, 100, 32); past[0] != 1792 || past[0] <= LimitMilliamps() {
		t.Fatalf("at 55 + 1 mA white draws %v", past)
	}
	if err := SetPower(defaults); err != nil {
		t.Fatal(err)
	}
	preview, err := NewPreview(white)
	if err != nil {
		t.Fatal(err)
	}
	preview.Frame(0, 100, make([]Color, 32))
	if preview.Milliamps() != full[0] {
		t.Fatalf("the preview says %d, a run %d", preview.Milliamps(), full[0])
	}
	if _, err := Currents(white, []uint32{0}, 100, 0); err == nil {
		t.Fatal("no LEDs drew a current")
	}
}
