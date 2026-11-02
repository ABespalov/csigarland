// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of dragging a colour before or after another in its line.
// Contributors: None

package ui

import (
	"image"
	"slices"
	"testing"

	"gioui.org/f32"
	"gioui.org/io/input"
	"gioui.org/io/pointer"
	"gioui.org/op"
)

// The gap the pointer is nearest: before an item whose middle is right of it, on its own line.
func TestTheGapUnderThePointer(t *testing.T) {
	items := []image.Rectangle{
		image.Rect(0, 0, 100, 30), image.Rect(120, 0, 220, 30), // First line.
		image.Rect(0, 40, 100, 70), // Second line.
	}
	for _, c := range []struct {
		at   image.Point
		want int
	}{
		{image.Pt(10, 10), 0}, {image.Pt(60, 10), 1}, {image.Pt(200, 10), 2}, {image.Pt(400, 10), 2},
		{image.Pt(10, 50), 2}, {image.Pt(90, 50), 3}, {image.Pt(10, 300), 2},
	} {
		if got := insertionAt(items, c.at); got != c.want {
			t.Errorf("at %v: gap %d, want %d", c.at, got, c.want)
		}
	}
	// A colour lands in the gap, counted without itself.
	if landing(2, 0) != 0 || landing(0, 3) != 2 || landing(1, 1) != 1 || landing(1, 2) != 1 {
		t.Error("landing", landing(2, 0), landing(0, 3), landing(1, 1), landing(1, 2))
	}
	if got := moveItem([]string{"a", "b", "c", "d"}, 3, 1); !slices.Equal(got, []string{"a", "d", "b", "c"}) {
		t.Errorf("moved %v", got)
	}
}

// A drag carries a colour, and dropping it before the first or after the last moves it there; a
// press without moving picks it as before.
func TestAColourIsDraggedBeforeOrAfterAnother(t *testing.T) {
	a := sceneApp(t, PageEffects)
	var line colourLine
	colours := []RGB{{255, 0, 0}, {0, 255, 0}, {0, 0, 255}}
	var router input.Router
	var moves [][2]int
	picked := -1
	frame := func() {
		var ops op.Ops
		gtx := sceneContext(&ops, &router, 900, 200)
		gtx.Constraints.Min = image.Point{}
		a.source = gtx.Source
		a.colourLine(gtx, &line, len(colours), func(i int) RGB { return colours[i] }, nil, "", nil,
			func(i int) { picked = i }, func(from, to int) { moves = append(moves, [2]int{from, to}) })
		router.Frame(&ops)
	}
	middle := func(i int) f32.Point {
		half := float32(swatchSize) / 2
		return f32.Pt(float32(line.swatches[i].X)+half, float32(line.swatches[i].Y)+half)
	}
	drag := func(from int, to f32.Point) {
		router.Queue(pointer.Event{Kind: pointer.Press, Source: pointer.Mouse, Buttons: pointer.ButtonPrimary, Position: middle(from)})
		frame()
		router.Queue(pointer.Event{Kind: pointer.Move, Source: pointer.Mouse, Buttons: pointer.ButtonPrimary, Position: to})
		frame()
		if !line.carrying || !line.moved {
			t.Fatalf("not carried: %+v", line)
		}
		router.Queue(pointer.Event{Kind: pointer.Release, Source: pointer.Mouse, Position: to})
		frame()
	}
	frame()
	drag(2, f32.Pt(float32(line.items[0].Min.X)+1, middle(0).Y))
	drag(0, f32.Pt(float32(line.items[2].Max.X)-1, middle(2).Y))
	if !slices.Equal(moves, [][2]int{{2, 0}, {0, 2}}) || line.carrying {
		t.Fatalf("moves %v, carrying %v", moves, line.carrying)
	}
	if picked != -1 {
		t.Fatalf("a drag picked colour %d", picked)
	}
	router.Queue(pointer.Event{Kind: pointer.Press, Source: pointer.Mouse, Buttons: pointer.ButtonPrimary, Position: middle(1)},
		pointer.Event{Kind: pointer.Release, Source: pointer.Mouse, Position: middle(1)})
	frame()
	frame()
	if picked != 1 || len(moves) != 2 {
		t.Fatalf("a press picked %d, moves %v", picked, moves)
	}
}
