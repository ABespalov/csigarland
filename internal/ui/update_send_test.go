// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: What an update sent from here goes by: the garland's own answer right before it, and what proves it took the file.
// Contributors: None

package ui

import (
	"net/http"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/ABespalov/csigarland/internal/api"
)

// The garland was updated elsewhere between the offer and the press: the fresh answer decides, not the
// version the window read before.
func TestAnOfferIsNotSentToAGarlandUpdatedSince(t *testing.T) {
	var posted atomic.Bool
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		if r.Method == http.MethodPost {
			posted.Store(true)
		}
		answer(t, w, api.Firmware{Version: "1.0.0.9", Boot: "b"})
	})
	a := sceneApp(t, PageFirmware)
	a.client = client
	a.about.Version = "1.0.0.5"
	a.firmware.file = []byte("update")
	a.firmware.send(a, "1.0.0.8")
	select {
	case <-a.firmware.session.Done():
	case <-time.After(10 * time.Second):
		t.Fatal("the worker did not end")
	}
	a.drain()
	if posted.Load() || a.firmware.busy || !strings.Contains(a.status, "by now") {
		t.Fatalf("posted %v, busy %v, %q", posted.Load(), a.firmware.busy, a.status)
	}
}

// Effects alone start no trial: one seen is another firmware's, sent from elsewhere.
func TestEffectsAloneAreNotConfirmedByAnotherFirmwaresTrial(t *testing.T) {
	start := time.Now()
	file := api.UpdateFile{HasEffects: true, PackDigest: "pack"}
	watch := updateWatch{update: file, bootBefore: "before", packBefore: "pack", imageBefore: "old", started: start, unknown: true}
	watch.look(start.Add(5*time.Second), api.Firmware{Boot: "after", Image: "other", PackDigest: "pack", OnTrial: true}, nil)
	said := watch.look(start.Add(70*time.Second), api.Firmware{Boot: "after", Image: "other", PackDigest: "pack"}, nil)
	if !said.finished || !said.failed {
		t.Fatalf("called its own: %+v", said)
	}
	// New effects, with the firmware as it was: those are this file's.
	taken := updateWatch{update: file, bootBefore: "before", packBefore: "old-pack", imageBefore: "old", started: start, unknown: true}
	if said := taken.look(start.Add(9*time.Second), api.Firmware{Boot: "after", Image: "old", PackDigest: "pack"}, nil); !said.finished || said.failed || !said.confirmed {
		t.Fatalf("%+v", said)
	}
	// New effects, and another firmware with them: someone else's update may have brought both.
	other := updateWatch{update: file, bootBefore: "before", packBefore: "old-pack", imageBefore: "old", started: start, unknown: true}
	if said := other.look(start.Add(9*time.Second), api.Firmware{Boot: "after", Image: "other", PackDigest: "pack"}, nil); !said.finished || !said.failed {
		t.Fatalf("%+v", said)
	}
}

func TestAConfirmedFirmwareUpdateOpensInfo(t *testing.T) {
	start := time.Now()
	watch := updateWatch{update: api.UpdateFile{HasFirmware: true, Image: "new"}, bootBefore: "before", started: start}
	confirmed := watch.look(start.Add(70*time.Second), api.Firmware{Boot: "after", Image: "new"}, nil)
	if !confirmed.finished || confirmed.failed || !confirmed.confirmed {
		t.Fatalf("the installed firmware was not confirmed: %+v", confirmed)
	}
	if !goesToInfoAfterUpdate(PageFirmware, confirmed, true) {
		t.Fatal("a confirmed firmware update stayed on Firmware")
	}
	if goesToInfoAfterUpdate(PageInfo, confirmed, true) || goesToInfoAfterUpdate(PageFirmware, confirmed, false) {
		t.Fatal("Info opened for an update that was not this firmware's")
	}
}

func TestACancelledUninstalledUploadStaysOnFirmware(t *testing.T) {
	start := time.Now()
	watch := updateWatch{update: api.UpdateFile{HasFirmware: true, Image: "new"}, bootBefore: "before", started: start, unknown: true, cancelled: true}
	same := api.Firmware{Boot: "before", Image: "old"}
	watch.look(start.Add(time.Second), same, nil)
	said := watch.look(start.Add(time.Second+updateSettle), same, nil)
	if !said.finished || said.failed || said.confirmed || !strings.Contains(said.words, "Not installed") {
		t.Fatalf("cancellation was not kept separate from confirmation: %+v", said)
	}
	if goesToInfoAfterUpdate(PageFirmware, said, true) {
		t.Fatal("a cancelled upload that was not installed opened Info")
	}
}

func TestAFailedUpdateStaysOnFirmware(t *testing.T) {
	start := time.Now()
	watch := updateWatch{update: api.UpdateFile{HasFirmware: true, Image: "new"}, bootBefore: "before", started: start}
	said := watch.look(start.Add(70*time.Second), api.Firmware{Boot: "after", Image: "old"}, nil)
	if !said.finished || !said.failed || said.confirmed {
		t.Fatalf("a rollback was confirmed: %+v", said)
	}
	if goesToInfoAfterUpdate(PageFirmware, said, true) {
		t.Fatal("a failed firmware update opened Info")
	}
}
