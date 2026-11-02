// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The segmented control: how its options go on lines, and that a press chooses one.
// Contributors: None

package ui

import (
	"slices"
	"testing"

	"gioui.org/f32"
	"gioui.org/io/input"
	"gioui.org/io/pointer"
	"gioui.org/op"
)

func TestSegmentsTakeAsFewLinesAsTheyCanAndShareThemEvenly(t *testing.T) {
	widths := []int{50, 60, 70, 80, 90}
	if rows := balanceSegmentRows(widths, 1000); len(rows) != 1 {
		t.Fatalf("all of them fit on a line, and took %d", len(rows))
	}
	// Filled to the edge, 300 would leave the last alone on a second line.
	rows := balanceSegmentRows(widths, 300)
	if len(rows) != 2 || rows[0].end-rows[0].start < 2 || rows[1].end-rows[1].start < 2 {
		t.Fatalf("%v", rows)
	}
	stretched, block := stretchSegmentRows(widths, rows)
	for _, row := range rows {
		sum := 0
		for i := row.start; i < row.end; i++ {
			sum += stretched[i]
		}
		if sum != block {
			t.Fatalf("a line of %d in a block of %d: %v", sum, block, stretched)
		}
	}
	// One wider than the limit still has a line of its own.
	if rows := packSegmentRows([]int{500, 10}, 100); !slices.Equal(rows, []segmentRow{{0, 1}, {1, 2}}) {
		t.Fatalf("%v", rows)
	}
}

func TestAPressChoosesItsSegmentButNotWhenItCannot(t *testing.T) {
	a := sceneApp(t, PageEffects)
	var ops op.Ops
	var router input.Router
	var mode choice
	mode.Value = "auto"
	draw := func(enabled bool) {
		ops.Reset()
		a.frames++
		gtx := sceneContext(&ops, &router, sceneWidth, sceneHeight)
		a.source = gtx.Source
		if !enabled {
			gtx = gtx.Disabled()
		}
		a.segments(gtx, &mode, ledModes)
		router.Frame(&ops)
	}
	// Just past the first option, which is the second's.
	measure := sceneContext(new(op.Ops), &router, sceneWidth, sceneHeight)
	second := a.segmentWidth(measure, ledModes[0].words) + 2*measure.Dp(segmentPad) + 5
	press := func(enabled bool) {
		draw(enabled)
		at := f32.Pt(float32(second), 10)
		router.Queue(
			pointer.Event{Kind: pointer.Press, Source: pointer.Mouse, Buttons: pointer.ButtonPrimary, Position: at},
			pointer.Event{Kind: pointer.Release, Source: pointer.Mouse, Position: at},
		)
		draw(enabled)
	}
	press(false)
	if mode.Value != "auto" {
		t.Fatalf("a press on the control drawn disabled chose %q", mode.Value)
	}
	press(true)
	if mode.Value != "manual" {
		t.Fatalf("the press chose %q", mode.Value)
	}
}
