// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Pictures of the utility's pages for the documents, drawn by Gio with no window.
// Contributors: None

//go:build utilityshot

package ui

import (
	"image"
	"image/draw"
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"gioui.org/gpu/headless"
	"gioui.org/io/input"
	"gioui.org/op"

	"github.com/ABespalov/csigarland/internal/api"
)

// CSIGARLAND_SHOTS names the folder. deploy/new-doc-images.ps1 and its bash twin set it.
//
// No two windows are open at once: on Linux Gio draws with EGL, whose display is one for the whole
// process, and releasing a window terminates it, leaving any other window unable to draw. So the short
// picture, in a window of its own height, is drawn and done with before the others' window opens.
func TestRenderShots(t *testing.T) {
	folder := os.Getenv("CSIGARLAND_SHOTS")
	if folder == "" {
		t.Skip("CSIGARLAND_SHOTS is not set")
	}
	shootShort(t, filepath.Join(folder, "effects-short.png"))
	window, err := headless.NewWindow(sceneWidth, sceneHeight)
	if err != nil {
		t.Fatalf("a window with no screen: %v", err)
	}
	defer window.Release()
	for page := PageEffects; page < pageCount; page++ {
		shoot(t, window, filepath.Join(folder, strings.ToLower(pageNames[page])+".png"), sceneApp(t, page))
	}
	sending := sceneApp(t, PageFirmware)
	sending.firmware.path = "csigarland-" + shotFirmware + ".ota"
	sending.firmware.contents = "Holds firmware " + shotFirmware + " and 154 effects."
	sending.firmware.file = []byte("update")
	sending.firmware.update = api.UpdateFile{HasFirmware: true, HasEffects: true}
	sending.firmware.busy = true
	sending.firmware.uploading = true
	sending.firmware.percent = new(atomic.Int32)
	sending.firmware.percent.Store(42)
	shoot(t, window, filepath.Join(folder, "firmware-sending.png"), sending)
	action := sceneApp(t, PageSchedule)
	action.schedule.openForm(0)
	action.schedule.form.useSpeed.Value = true
	shoot(t, window, filepath.Join(folder, "schedule-action.png"), action)
	action.schedule.form.clock.open(17, 0)
	action.schedule.popup = popupTime
	shoot(t, window, filepath.Join(folder, "schedule-time.png"), action)
	action.schedule.form.chooser.open(action.schedule.effects, action.schedule.form.effect)
	action.schedule.popup = popupEffect
	shoot(t, window, filepath.Join(folder, "schedule-effect.png"), action)
	action.schedule.clockPick.open(startTime(action.schedule.clock, time.Now()))
	action.schedule.popup = popupClock
	shoot(t, window, filepath.Join(folder, "schedule-clock.png"), action)
	shoot(t, window, filepath.Join(folder, "tune.png"), sceneTune(t))
	shoot(t, window, filepath.Join(folder, "tune-colours.png"), sceneTuneColours(t))
	shoot(t, window, filepath.Join(folder, "preview.png"), scenePreview(t))
	shoot(t, window, filepath.Join(folder, "no-garland.png"), sceneLocal(t))
	shoot(t, window, filepath.Join(folder, "connect.png"), sceneConnect(t))
	connect := newApp(shotUtility)
	connect.connect.stage = stageCode
	connect.connect.code = "4821"
	connect.connect.seconds = 60
	shoot(t, window, filepath.Join(folder, "pair.png"), connect)
	connect.stop()
}

// shortHeight is a window tall enough for all of a short page, which is then cut below its last card.
const shortHeight = 2400

// shootShort is the Effects page with its first group open and the others closed, cut below them, the
// footer kept: the rest of a long list says nothing more, and makes a picture too tall to read at a glance.
// The list stays whole, so that the playlist and the count of effects say what they do on a garland.
func shootShort(t *testing.T, path string) {
	t.Helper()
	window, err := headless.NewWindow(sceneWidth, shortHeight)
	if err != nil {
		t.Fatalf("a window with no screen: %v", err)
	}
	defer window.Release()
	a := released(sceneApp(t, PageEffects))
	first := sceneEffects()[0].Group
	for _, effect := range sceneEffects() {
		a.effects.opened[effect.Group] = &groupState{known: true, open: effect.Group == first}
	}
	var ops op.Ops
	var router input.Router
	for range 2 {
		ops.Reset()
		a.Frame(sceneContext(&ops, &router, sceneWidth, shortHeight))
		router.Frame(&ops)
	}
	if err := window.Frame(&ops); err != nil {
		t.Fatalf("draw %s: %v", path, err)
	}
	full := image.NewRGBA(image.Rect(0, 0, sceneWidth, shortHeight))
	if err := window.Screenshot(full); err != nil {
		t.Fatalf("take %s: %v", path, err)
	}
	// The footer starts below the last row of the window's own background, and the page ends at the last
	// row above that with anything on it.
	plain := func(y int) bool {
		for x := 0; x < sceneWidth; x++ {
			r, g, b, _ := full.At(x, y).RGBA()
			if uint8(r>>8) != colorBackground.R || uint8(g>>8) != colorBackground.G || uint8(b>>8) != colorBackground.B {
				return false
			}
		}
		return true
	}
	footer := shortHeight - 1
	for footer > 0 && !plain(footer-1) {
		footer--
	}
	end := footer - 1
	for end > 0 && plain(end) {
		end--
	}
	if end <= 0 || footer <= 0 {
		t.Fatalf("%s: no page and footer to tell apart", path)
	}
	keep := end + 1 + int(gapPage)
	shot := image.NewRGBA(image.Rect(0, 0, sceneWidth, keep+shortHeight-footer))
	draw.Draw(shot, image.Rect(0, 0, sceneWidth, keep), full, image.Point{}, draw.Src)
	draw.Draw(shot, image.Rect(0, keep, sceneWidth, shot.Bounds().Dy()), full, image.Pt(0, footer), draw.Src)
	file, err := os.Create(path)
	if err != nil {
		t.Fatalf("create %s: %v", path, err)
	}
	defer file.Close()
	if err := png.Encode(file, shot); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

// Versions of the release the pictures show. The scenes keep older ones, which the tests of updates and the
// catalog rely on.
const (
	shotUtility  = "1.4.0.51"
	shotFirmware = "1.4.0.119"
)

// released is a scene with the utility and the garland on the release the pictures show.
func released(a *App) *App {
	a.version = shotUtility
	if a.about.Version != "" {
		a.about.Version = shotFirmware
	}
	if a.firmware.running.Version != "" {
		a.firmware.running.Version = shotFirmware
	}
	return a
}

func shoot(t *testing.T, window *headless.Window, path string, a *App) {
	t.Helper()
	released(a)
	var ops op.Ops
	var router input.Router
	for range 2 {
		ops.Reset()
		a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight))
		router.Frame(&ops)
	}
	if err := window.Frame(&ops); err != nil {
		t.Fatalf("draw %s: %v", path, err)
	}
	shot := image.NewRGBA(image.Rect(0, 0, sceneWidth, sceneHeight))
	if err := window.Screenshot(shot); err != nil {
		t.Fatalf("take %s: %v", path, err)
	}
	file, err := os.Create(path)
	if err != nil {
		t.Fatalf("create %s: %v", path, err)
	}
	defer file.Close()
	if err := png.Encode(file, shot); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}
