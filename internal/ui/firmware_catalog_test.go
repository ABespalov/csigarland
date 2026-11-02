// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the catalog of releases: listed on the Firmware page, and a newer one offered after connecting.
// Contributors: None

package ui

import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
	"time"

	"gioui.org/io/input"
	"gioui.org/op"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/catalog"
)

// An update file of firmware alone, as the header reads it: magic, format, the parts' sizes.
func sampleUpdate() []byte {
	const header, certificate = 148, 10
	file := make([]byte, header+certificate)
	copy(file, "CGOT")
	binary.LittleEndian.PutUint32(file[4:], 3)
	binary.LittleEndian.PutUint32(file[8:], certificate)
	image := []byte(strings.Repeat("image", 20))
	binary.LittleEndian.PutUint32(file[16:], uint32(len(image)))
	return append(file, image...)
}

func untilTrue(t *testing.T, a *App, done func() bool) {
	t.Helper()
	deadline := time.Now().Add(10 * time.Second)
	for !done() {
		if time.Now().After(deadline) {
			t.Fatal("it never came")
		}
		a.drain()
		time.Sleep(time.Millisecond)
	}
}

func TestAReleaseIsDownloadedFromTheCatalogAndReadyToInstall(t *testing.T) {
	file := sampleUpdate()
	sum := sha256.Sum256(file)
	var server *httptest.Server
	server = httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/catalog.json" {
			fmt.Fprintf(w, `{"format":%q,"firmware":[{"version":"1.0.0.30","file":"csigarland-1.0.0.30.ota","url":%q,"sha256":%q,"bytes":%d,"effects":154,"published":"2026-12-01"}]}`,
				catalog.Format, server.URL+"/file.ota", hex.EncodeToString(sum[:]), len(file))
			return
		}
		_, _ = w.Write(file)
	}))
	defer server.Close()

	a := sceneApp(t, PageFirmware)
	page := &a.firmware
	page.running = api.Firmware{Version: "1.0.0.27"}
	page.releases.address = server.URL + "/catalog.json"
	page.releases.http = server.Client()
	frame := func() {
		var ops op.Ops
		var router input.Router
		a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight))
	}
	frame()
	page.releases.open.Click()
	frame()
	untilTrue(t, a, func() bool { return page.releases.shown })
	if len(page.releases.entries) != 1 || page.releases.entries[0].Version != "1.0.0.30" {
		t.Fatalf("%+v", page.releases.entries)
	}
	frame()
	page.releases.picks["1.0.0.30"].Click()
	frame()
	untilTrue(t, a, func() bool { return page.file != nil })
	if page.path != "csigarland-1.0.0.30.ota" || !page.update.HasFirmware || page.releases.shown || !strings.Contains(page.contents, "catalog") {
		t.Fatalf("path %q, update %+v, shown %v, contents %q", page.path, page.update, page.releases.shown, page.contents)
	}
}

// No test reaches the real catalog: those that want one name their own.
func TestMain(m *testing.M) {
	releasesAddress = ""
	os.Exit(m.Run())
}

// Connected to a garland that runs an older firmware, the window offers the newest release, and Install
// downloads it and sends it to the garland.
func TestANewerReleaseIsOfferedAndInstalledAfterConnecting(t *testing.T) {
	file := sampleUpdate()
	sum := sha256.Sum256(file)
	var releases *httptest.Server
	releases = httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/catalog.json" {
			fmt.Fprintf(w, `{"format":%q,"firmware":[{"version":"1.0.0.102","file":"csigarland-1.0.0.102.ota","url":%q,"sha256":%q,"bytes":%d,"effects":154,"published":"2026-11-03"}]}`,
				catalog.Format, releases.URL+"/file.ota", hex.EncodeToString(sum[:]), len(file))
			return
		}
		_, _ = w.Write(file)
	}))
	defer releases.Close()
	installed := make(chan []byte, 1)
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		switch {
		case r.URL.Path == "/api/v1/firmware" && r.Method == http.MethodPost:
			body, _ := io.ReadAll(r.Body)
			installed <- body
			answer(t, w, map[string]any{})
		case r.URL.Path == "/api/v1/firmware":
			answer(t, w, api.Firmware{Version: "1.0.0.101", Effects: 154, Boot: "one"})
		default:
			http.NotFound(w, r)
		}
	})

	a := newApp("1.0.0")
	defer a.stop()
	a.firmware.releases.address = releases.URL + "/catalog.json"
	a.firmware.releases.http = releases.Client()
	a.useClient(client)
	untilTrue(t, a, func() bool { return a.modal == modalOffer })
	if a.offer.entry.Version != "1.0.0.102" || a.offer.running != "1.0.0.101" {
		t.Fatalf("offered %q to %q", a.offer.entry.Version, a.offer.running)
	}
	frame := func() {
		var ops op.Ops
		var router input.Router
		a.Frame(sceneContext(&ops, &router, sceneWidth, sceneHeight))
	}
	frame()
	a.offer.install.Click()
	frame()
	if a.modal != modalNone || a.page != PageFirmware {
		t.Fatalf("modal %v, page %v", a.modal, a.page)
	}
	var sent []byte
	untilTrue(t, a, func() bool {
		select {
		case sent = <-installed:
			return true
		default:
			return false
		}
	})
	if !bytes.Equal(sent, file) || a.firmware.path != "csigarland-1.0.0.102.ota" {
		t.Fatalf("sent %d bytes, page has %q", len(sent), a.firmware.path)
	}
}

// What the garland runs is the newest release, so nothing is offered; and a garland left takes its offer with it.
func TestNothingIsOfferedForTheNewestRelease(t *testing.T) {
	releases := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, `{"format":%q,"firmware":[{"version":"1.0.0.102","file":"csigarland-1.0.0.102.ota","url":"https://example.com/a.ota","sha256":%q,"bytes":10,"effects":154,"published":"2026-11-03"}]}`,
			catalog.Format, strings.Repeat("0", 64))
	}))
	defer releases.Close()
	asked := make(chan struct{}, 4)
	client := stubGarland(t, func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/api/v1/firmware" {
			answer(t, w, api.Firmware{Version: "1.0.0.102"})
			asked <- struct{}{}
			return
		}
		http.NotFound(w, r)
	})
	a := newApp("1.0.0")
	defer a.stop()
	a.firmware.releases.address = releases.URL + "/catalog.json"
	a.firmware.releases.http = releases.Client()
	a.useClient(client)
	<-asked
	settleWork(t, a)
	time.Sleep(200 * time.Millisecond)
	a.drain()
	if a.modal != modalNone || !a.offer.looked {
		t.Fatalf("modal %v, looked %v", a.modal, a.offer.looked)
	}
	a.disconnect()
	if a.offer.looked {
		t.Fatal("the look stayed after the garland was left")
	}
}
