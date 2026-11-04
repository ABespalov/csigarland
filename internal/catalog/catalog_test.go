// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of reading the firmware catalog and downloading what it lists.
// Contributors: None

package catalog

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func entryFor(server *httptest.Server, version string, data []byte) Entry {
	sum := sha256.Sum256(data)
	return Entry{Version: version, File: "csigarland-" + version + ".ota", URL: server.URL + "/" + version,
		SHA256: hex.EncodeToString(sum[:]), Bytes: int64(len(data)), Effects: 154, Published: "2026-11-03"}
}

func catalogJSON(entries ...Entry) string {
	parts := make([]string, len(entries))
	for i, e := range entries {
		parts[i] = fmt.Sprintf(`{"version":%q,"file":%q,"url":%q,"sha256":%q,"bytes":%d,"effects":%d,"published":%q}`,
			e.Version, e.File, e.URL, e.SHA256, e.Bytes, e.Effects, e.Published)
	}
	return `{"format":"` + Format + `","firmware":[` + strings.Join(parts, ",") + `]}`
}

// The catalog is read newest first, and a file comes down only as the catalog names it.
func TestTheCatalogListsWhatADownloadIsHeldTo(t *testing.T) {
	files := map[string][]byte{"1.0.0.27": []byte("first release"), "1.0.0.30": []byte("a later one")}
	var server *httptest.Server
	server = httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/catalog.json" {
			_, _ = w.Write([]byte(catalogJSON(entryFor(server, "1.0.0.27", files["1.0.0.27"]), entryFor(server, "1.0.0.30", files["1.0.0.30"]))))
			return
		}
		_, _ = w.Write(files[strings.TrimPrefix(r.URL.Path, "/")])
	}))
	defer server.Close()
	ctx := context.Background()
	read, err := Fetch(ctx, server.Client(), server.URL+"/catalog.json")
	if err != nil {
		t.Fatal(err)
	}
	if len(read.Firmware) != 2 || read.Firmware[0].Version != "1.0.0.30" {
		t.Fatalf("%+v", read.Firmware)
	}
	data, err := Download(ctx, server.Client(), read.Firmware[1])
	if err != nil || string(data) != "first release" {
		t.Fatalf("%q, %v", data, err)
	}
	// Another file at the same place is not the one named.
	files["1.0.0.27"] = []byte("first releasX")
	if _, err := Download(ctx, server.Client(), read.Firmware[1]); err == nil || !strings.Contains(err.Error(), "SHA-256") {
		t.Fatalf("a changed file came down: %v", err)
	}
	files["1.0.0.27"] = []byte("first release, and more")
	if _, err := Download(ctx, server.Client(), read.Firmware[1]); err == nil {
		t.Fatal("a longer file came down")
	}
}

func TestACatalogThatCannotBeTrustedIsRefused(t *testing.T) {
	good := Entry{Version: "1.0.0.27", File: "csigarland-1.0.0.27.ota", URL: "https://github.com/x/y/releases/download/v1/a.ota",
		SHA256: strings.Repeat("ab", 32), Bytes: 100}
	for name, body := range map[string]string{
		"another format":   strings.Replace(catalogJSON(good), Format, "csigarland.firmware-catalog/2", 1),
		"plain http":       catalogJSON(Entry{Version: good.Version, File: good.File, URL: "http://example.com/a.ota", SHA256: good.SHA256, Bytes: 100}),
		"no hash":          catalogJSON(Entry{Version: good.Version, File: good.File, URL: good.URL, SHA256: "ab", Bytes: 100}),
		"too large":        catalogJSON(Entry{Version: good.Version, File: good.File, URL: good.URL, SHA256: good.SHA256, Bytes: MaxFileBytes + 1}),
		"a path for name":  catalogJSON(Entry{Version: good.Version, File: `..\a.ota`, URL: good.URL, SHA256: good.SHA256, Bytes: 100}),
		"not a version":    catalogJSON(Entry{Version: "latest", File: good.File, URL: good.URL, SHA256: good.SHA256, Bytes: 100}),
		"an unknown field": strings.Replace(catalogJSON(good), `"format"`, `"extra":1,"format"`, 1),
		"trailing bytes":   catalogJSON(good) + "{}",
		"a bracket after":  catalogJSON(good) + "]",
		"a brace after":    catalogJSON(good) + "}",
	} {
		if _, err := Parse([]byte(body)); err == nil {
			t.Errorf("%s was taken", name)
		}
	}
	if _, err := Parse([]byte(catalogJSON(good) + "\n \n")); err != nil {
		t.Errorf("a good catalog: %v", err)
	}
}

// A file the catalog names is taken only if it holds firmware of the version its row names: a signed one
// of another, an older among them, would install that.
func TestADownloadHoldsTheVersionItsRowNames(t *testing.T) {
	entry := Entry{Version: "1.1.0.107", File: "csigarland-1.1.0.107.ota"}
	if err := HoldsVersion(entry, true, "1.1.0.107"); err != nil {
		t.Errorf("the right file: %v", err)
	}
	if err := HoldsVersion(entry, true, "1.0.0.102"); err == nil {
		t.Error("an older firmware under a newer row was taken")
	}
	if err := HoldsVersion(entry, false, ""); err == nil {
		t.Error("effects alone under a row of firmware were taken")
	}
}

func TestVersionsCompareAsNumbers(t *testing.T) {
	if !Newer("1.0.0.30", "1.0.0.27") || Newer("1.0.0.27", "1.0.0.27") || !Newer("1.0.1.0", "1.0.0.99") || Newer("junk", "1.0.0.1") {
		t.Fatal("versions compare wrongly")
	}
}

// A garland is offered the newest release only when it is newer than what it runs, a Dev build by its number.
func TestADevBuildIsItsNumberWhereverVersionsAreCompared(t *testing.T) {
	if Newer("1.0.0.27", "1.0.0.27-dev") || !Newer("1.0.0.28", "1.0.0.27-dev") || Newer("1.0.0.26", "1.0.0.27-dev") {
		t.Fatal("a Dev build was not taken as its number")
	}
}

func TestOnlyANewerReleaseIsOffered(t *testing.T) {
	read := Catalog{Format: Format, Firmware: []Entry{{Version: "1.0.0.102"}, {Version: "1.0.0.101"}}}
	cases := []struct {
		running string
		offered bool
	}{
		{"1.0.0.101", true},
		{"0.1.0.101-dev", true},
		{"1.0.0.101-dev", true},
		{"1.0.0.102", false},
		{"1.0.0.102-dev", false},
		{"1.0.0.103", false},
		{"", false},
		{"junk", false},
	}
	for _, c := range cases {
		entry, offered := Offer(read, c.running)
		if offered != c.offered || (offered && entry.Version != "1.0.0.102") {
			t.Errorf("running %q: offered %v %q", c.running, offered, entry.Version)
		}
	}
	if _, offered := Offer(Catalog{Format: Format}, "1.0.0.1"); offered {
		t.Error("an empty catalog offered something")
	}
}
