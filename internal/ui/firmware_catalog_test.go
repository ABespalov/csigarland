// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the catalog of releases: listed on the Firmware page, and a newer one offered after connecting.
// Contributors: None

package ui

import (
	"bytes"
	"context"
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

// An update file of firmware alone, of version, as the header reads it: magic, format, the parts' sizes,
// and the image's description at its place, with the version it holds.
func sampleUpdate(version string) []byte {
	const header, certificate = 148, 10
	file := make([]byte, header+certificate)
	copy(file, "CGOT")
	binary.LittleEndian.PutUint32(file[4:], 3)
	binary.LittleEndian.PutUint32(file[8:], certificate)
	image := make([]byte, 32+176+16)
	binary.LittleEndian.PutUint32(image[32:], 0xABCD5432)
	copy(image[32+16:], version)
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
	file := sampleUpdate("1.0.0.30")
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
	file := sampleUpdate("1.0.0.102")
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

// A download from the catalog that comes after the owner chose another file leaves that choice, and
// sends nothing, though it was asked to install.
func TestALateDownloadLeavesAFileChosenSince(t *testing.T) {
	file := sampleUpdate("1.0.0.102")
	sum := sha256.Sum256(file)
	release := make(chan struct{})
	var server *httptest.Server
	server = httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		<-release
		_, _ = w.Write(file)
	}))
	defer server.Close()
	a := sceneApp(t, PageFirmware)
	page := &a.firmware
	page.releases.http = server.Client()
	entry := catalog.Entry{Version: "1.0.0.102", File: "csigarland-1.0.0.102.ota", URL: server.URL + "/a.ota",
		SHA256: hex.EncodeToString(sum[:]), Bytes: int64(len(file))}
	page.download(a, entry, true)
	// The owner's own file is picked, and its reading, from a slow disk, outlasts the download.
	savedRead := readUpdateFile
	picked, reading := make(chan struct{}), make(chan struct{})
	pickUpdateFile = func(context.Context, uintptr, string, string, string) (string, error) {
		close(picked)
		return "mine.ota", nil
	}
	readUpdateFile = func(string) ([]byte, error) {
		<-reading
		return file, nil
	}
	t.Cleanup(func() { pickUpdateFile, readUpdateFile = chooseFile, savedRead })
	page.chooseFile(a)
	<-picked
	close(release)
	untilTrue(t, a, func() bool { return !page.releases.fetching })
	if page.busy || page.file != nil {
		t.Fatalf("the download was taken: busy %v, path %q", page.busy, page.path)
	}
	close(reading)
	untilTrue(t, a, func() bool { return !a.choosing })
	if page.path != "mine.ota" || page.busy {
		t.Fatalf("path %q, busy %v", page.path, page.busy)
	}
}
