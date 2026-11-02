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

// CSIGARLAND_SHOTS names the folder. deploy/new-doc-images.ps1 sets it.
func TestRenderShots(t *testing.T) {
	folder := os.Getenv("CSIGARLAND_SHOTS")
	if folder == "" {
		t.Skip("CSIGARLAND_SHOTS is not set")
	}
	window, err := headless.NewWindow(sceneWidth, sceneHeight)
	if err != nil {
		t.Fatalf("a window with no screen: %v", err)
	}
	defer window.Release()
	for page := PageEffects; page < pageCount; page++ {
		shoot(t, window, filepath.Join(folder, strings.ToLower(pageNames[page])+".png"), sceneApp(t, page))
	}
	sending := sceneApp(t, PageFirmware)
	sending.firmware.path = "csigarland-1.0.0.28.ota"
	sending.firmware.contents = "Holds firmware 1.0.0.28 and 154 effects."
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
	connect := newApp("1.0.0")
	connect.connect.stage = stageCode
	connect.connect.code = "4821"
	connect.connect.seconds = 60
	shoot(t, window, filepath.Join(folder, "pair.png"), connect)
	connect.stop()
}

func shoot(t *testing.T, window *headless.Window, path string, a *App) {
	t.Helper()
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
