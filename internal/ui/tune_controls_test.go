// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of Tune's controls as they are pressed: choosers, the picker, and a new effect.
// Contributors: None

package ui

import (
	"slices"
	"strings"
	"testing"
	"time"

	"gioui.org/io/input"
	"gioui.org/op"
)

func tuneFrame(a *App) {
	var ops op.Ops
	var router input.Router
	a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight))
}

// A press takes two frames: one to take it and write the file, one to show what the file says.
func pressAndSee(a *App, press func()) {
	press()
	tuneFrame(a)
	tuneFrame(a)
}

const rainFile = `{"format":"csigarland.effect/1","name":"Drops","layers":[{"fx":"rain","col":["103850"]}]}`

// Each option of a chooser writes the file, and the chooser then shows what the file says.
func TestThePaletteAndTheZoneChangeWhenPressed(t *testing.T) {
	a := sceneTune(t)
	a.tune.setFile(rainFile)
	tuneFrame(a)
	c := &a.tune.layers[0]
	for _, step := range []struct {
		button    int
		chooser   *choice
		want, has string
	}{
		{1, &c.palette, "rainbow", `"pal": "Rainbow"`},
		{2, &c.palette, "gradient", `"pal": [0,"103850",255,"103850"]`},
		{0, &c.palette, "colours", `"fx":"rain"`},
		{2, &c.zone, "every", `"zone": {"every":2,"offset":0}`},
		{1, &c.zone, "figurines", `"zone": ["`},
		{0, &c.zone, "all", `"fx":"rain"`},
	} {
		pressAndSee(a, step.chooser.button(step.button).Click)
		if step.chooser.Value != step.want || !strings.Contains(a.tune.file.Text(), step.has) {
			t.Fatalf("pressed %d: %q, in %s", step.button, step.chooser.Value, a.tune.file.Text())
		}
	}
	if strings.Contains(a.tune.file.Text(), "pal") || strings.Contains(a.tune.file.Text(), "zone") {
		t.Fatalf("Everywhere and From colours left %s", a.tune.file.Text())
	}
}

// The engine says what an algorithm reads: rain draws its drops from its palette, so its colours count
// only while the palette is made of them, and solid stands still at any pace.
func TestOnlyWhatAnAlgorithmReadsHasAControl(t *testing.T) {
	rain := algorithmUse("rain")
	if !rain.pal || rain.Colours(false) == 0 || rain.Colours(true) != 0 {
		t.Fatalf("rain %+v", rain)
	}
	if solid := algorithmUse("solid"); solid.sx {
		t.Fatalf("solid %+v", solid)
	}
	// Chase runs in its first colour; sparkle has sparks of the first on a ground of the second.
	if chase := algorithmUse("chase"); !chase.sx || chase.col != 1 {
		t.Fatalf("chase %+v", chase)
	}
	if sparkle := algorithmUse("sparkle"); sparkle.col != 2 {
		t.Fatalf("sparkle %+v", sparkle)
	}
	if unknown := algorithmUse("no-such-algorithm"); !unknown.sx || unknown.colPal != maxLayerColors || !unknown.pal {
		t.Fatalf("a name the parser refuses hides controls: %+v", unknown)
	}
	// No third colour for sparkle, which would not read it.
	a := sceneTune(t)
	twoColours := `{"format":"csigarland.effect/1","name":"S","layers":[{"fx":"sparkle","col":["40FFD0","304050"]}]}`
	a.tune.setFile(twoColours)
	tuneFrame(a)
	pressAndSee(a, a.tune.layers[0].addColor.Click)
	if a.tune.file.Text() != twoColours {
		t.Fatalf("a colour sparkle does not read was added: %s", a.tune.file.Text())
	}
	// Colours rain would not use are said to be so, and can be taken out.
	a.tune.setFile(`{"format":"csigarland.effect/1","name":"Drops","layers":[{"fx":"rain","col":["103850"],"pal":"Rainbow"}]}`)
	tuneFrame(a)
	pressAndSee(a, a.tune.layers[0].dropUnused[0].Click)
	if strings.Contains(a.tune.file.Text(), "col") || !strings.Contains(a.tune.file.Text(), `"pal":"Rainbow"`) {
		t.Fatalf("%s", a.tune.file.Text())
	}
}

// The picker is a window over Tune: a colour opens it, Done closes it, and Remove takes the colour out
// by a second press.
func TestThePickerIsAWindowOfItsOwn(t *testing.T) {
	a := sceneTune(t)
	a.tune.setFile(rainFile)
	tuneFrame(a)
	c := &a.tune.layers[0]
	pressAndSee(a, c.cols.slots[0].click.Click)
	if !a.tune.colors.open || a.tune.colors.at.Field != "col" {
		t.Fatalf("the picker is not open on the colour: %+v", a.tune.colors.at)
	}
	pressAndSee(a, a.tune.colors.done.Click)
	if a.tune.colors.open || a.modal != modalTune {
		t.Fatalf("Done left the picker open %v, or closed Tune: %v", a.tune.colors.open, a.modal)
	}
	pressAndSee(a, c.cols.slots[0].click.Click)
	pressAndSee(a, a.tune.colors.remove.click.Click)
	if !strings.Contains(a.tune.file.Text(), "103850") {
		t.Fatal("one press removed the colour")
	}
	pressAndSee(a, a.tune.colors.remove.click.Click)
	if a.tune.colors.open || strings.Contains(a.tune.file.Text(), "col") {
		t.Fatalf("open %v: %s", a.tune.colors.open, a.tune.file.Text())
	}
	// A gradient keeps its two stops.
	a.tune.setFile(`{"format":"csigarland.effect/1","name":"Drops","layers":[{"fx":"rain","pal":[0,"FF0000",255,"0000FF"]}]}`)
	tuneFrame(a)
	pressAndSee(a, c.stopLine.slots[1].click.Click)
	pressAndSee(a, a.tune.colors.remove.click.Click)
	pressAndSee(a, a.tune.colors.remove.click.Click)
	if !strings.Contains(a.tune.file.Text(), `"0000FF"`) {
		t.Fatalf("a stop of two was removed: %s", a.tune.file.Text())
	}
	pressAndSee(a, c.stopLine.slots[1].click.Click)
	// What is not saved takes a second press to close.
	a.closeModal(time.Now())
	a.closeModal(time.Now())
	if a.modal != modalNone || a.tune.colors.open {
		t.Fatal("the picker stayed open with Tune closed")
	}
}

// A new effect opens Tune on a file the firmware takes, with nothing yet to lose by closing it.
func TestANewEffectStartsInTune(t *testing.T) {
	a := sceneApp(t, PageEffects)
	pressAndSee(a, a.effects.newEffect.Click)
	if a.modal != modalTune || a.tune.file.Text() != newEffectFile || a.tune.preview == nil || a.tune.dirty() {
		t.Fatalf("modal %v, preview %v, dirty %v: %s", a.modal, a.tune.preview != nil, a.tune.dirty(), a.tune.file.Text())
	}
	if a.tune.model == nil || a.tune.id.Text() != "my-effect" {
		t.Fatalf("id %q", a.tune.id.Text())
	}
}

// A new stop goes after the last, as the plus at the end of the line says, and every stop before it
// keeps its number and its colour.
func TestANewStopGoesAfterTheLast(t *testing.T) {
	a := sceneTune(t)
	a.tune.setFile(`{"format":"csigarland.effect/1","name":"G","layers":[{"fx":"rain","pal":[0,"E00000",85,"00A000",170,"0030F0",255,"FFA000"]}]}`)
	tuneFrame(a)
	pressAndSee(a, a.tune.layers[0].addStop.Click)
	if want := `[0,"E00000",85,"00A000",170,"0030F0",212,"FFA000",255,"FFA000"]`; !strings.Contains(a.tune.file.Text(), want) {
		t.Fatalf("want %s in %s", want, a.tune.file.Text())
	}
	// Short of 255, the last stays where it is.
	if places, _, room := appendStop([]int{0, 200}, []string{"000000", "FFFFFF"}); !room || !slices.Equal(places, []int{0, 200, 255}) {
		t.Fatalf("%v %v", places, room)
	}
	// Side by side at the end, there is no room to make.
	if _, _, room := appendStop([]int{0, 254, 255}, []string{"000000", "808080", "FFFFFF"}); room {
		t.Fatal("a stop was made where there is no room")
	}
}

// A file of five layers is read, with controls for the four the firmware takes, and draws.
func TestAFileOfFiveLayersDoesNotTakeTheWindowDown(t *testing.T) {
	a := sceneTune(t)
	five := `{"format":"csigarland.effect/1","name":"Five","layers":[{"fx":"solid"},{"fx":"solid"},{"fx":"solid"},{"fx":"solid"},{"fx":"solid"}]}`
	a.tune.setFile(five)
	tuneFrame(a)
	if a.tune.model == nil || a.tune.model.Layers() != 5 || a.tune.problem == "" {
		t.Fatalf("model %v, problem %q", a.tune.model != nil, a.tune.problem)
	}
	// The fourth can still be removed by its control, which mends the file.
	a.tune.write(a.tune.model.RemoveLayer(4))
	tuneFrame(a)
	if a.tune.model.Layers() != 4 || a.tune.problem != "" {
		t.Fatalf("%d layers, problem %q", a.tune.model.Layers(), a.tune.problem)
	}
}

// A key spelled with an escape is still that key, and taking it out takes out nothing else.
func TestAnEscapedKeyIsTakenOutAlone(t *testing.T) {
	file := `{"format":"csigarland.effect/1","name":"N","layers":[{"fx":"rain","pal":"Rainbow"},{"fx":"rain","pal":"Rainbow"}]}`
	model, err := ReadEffect([]byte(file))
	if err != nil {
		t.Fatal(err)
	}
	want := `{"format":"csigarland.effect/1","name":"N","layers":[{"fx":"rain","pal":"Rainbow"},{"fx":"rain"}]}`
	if got := string(model.DeleteField(1, "pal")); got != want {
		t.Fatalf("got %s", got)
	}
}
