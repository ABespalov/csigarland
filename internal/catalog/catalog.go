// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The online catalog of firmware releases: reading it, and downloading an update file it lists.
// Contributors: None

// Package catalog reads the list of released update files the project's site publishes, and
// downloads one, held to the length and SHA-256 the list gives. It trusts neither to say whether a
// file is good: the garland checks the file's signature under its own root before it writes a byte.
// The list only says where a file is and which bytes are meant.
package catalog

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"slices"
	"strconv"
	"strings"
	"time"
)

const (
	// DefaultURL is where the site publishes the catalog.
	DefaultURL = "https://garland.csilab.am/downloads/firmware-catalog-v1.json"
	// Format is the catalog's own format, which a reader refuses if it is another.
	Format = "csigarland.firmware-catalog/1"

	maxCatalogBytes = 64 << 10
	// MaxFileBytes bounds an update file: two app slots' worth, more than any image and pack.
	MaxFileBytes = 4 << 20
	// Timeout bounds one request of either kind.
	Timeout = 2 * time.Minute
)

// Entry is one released update file.
type Entry struct {
	Version   string `json:"version"`
	File      string `json:"file"`
	URL       string `json:"url"`
	SHA256    string `json:"sha256"`
	Bytes     int64  `json:"bytes"`
	Effects   int    `json:"effects"`
	Published string `json:"published"`
}

// Catalog is the list, newest first once read.
type Catalog struct {
	Format   string  `json:"format"`
	Firmware []Entry `json:"firmware"`
}

// ErrFormat says the site's catalog is not one this program reads.
var ErrFormat = errors.New("the catalog is not in a format this utility reads")

// Fetch reads the catalog at address, bounded in size, and refuses one with an entry it could not
// download safely.
func Fetch(ctx context.Context, client *http.Client, address string) (Catalog, error) {
	body, err := get(ctx, client, address, maxCatalogBytes)
	if err != nil {
		return Catalog{}, fmt.Errorf("read the catalog: %w", err)
	}
	return Parse(body)
}

// Parse reads a catalog's bytes: one JSON value of the known format, every entry whole.
func Parse(body []byte) (Catalog, error) {
	decoder := json.NewDecoder(bytes.NewReader(body))
	decoder.DisallowUnknownFields()
	var read Catalog
	if err := decoder.Decode(&read); err != nil {
		return Catalog{}, fmt.Errorf("the catalog is not valid: %w", err)
	}
	if decoder.More() {
		return Catalog{}, errors.New("the catalog is not valid: something follows it")
	}
	if read.Format != Format {
		return Catalog{}, ErrFormat
	}
	for _, entry := range read.Firmware {
		if err := entry.check(); err != nil {
			return Catalog{}, fmt.Errorf("the catalog's entry %q: %w", entry.Version, err)
		}
	}
	slices.SortStableFunc(read.Firmware, func(a, b Entry) int { return compareVersions(b.Version, a.Version) })
	return read, nil
}

func (e Entry) check() error {
	where, err := url.Parse(e.URL)
	switch {
	case e.Version == "" || versionParts(e.Version) == nil:
		return errors.New("no version of four numbers")
	case err != nil || where.Scheme != "https" || where.Host == "":
		return errors.New("its file is not at an https address")
	case !strings.HasSuffix(e.File, ".ota") || strings.ContainsAny(e.File, `/\`):
		return errors.New("its file is not an update file's name")
	case len(e.SHA256) != sha256.Size*2 || !isHex(e.SHA256):
		return errors.New("no SHA-256")
	case e.Bytes <= 0 || e.Bytes > MaxFileBytes:
		return fmt.Errorf("a size of %d bytes", e.Bytes)
	}
	return nil
}

func isHex(text string) bool {
	_, err := hex.DecodeString(text)
	return err == nil
}

// Download gets an entry's file and holds it to the entry: its length and its SHA-256.
func Download(ctx context.Context, client *http.Client, entry Entry) ([]byte, error) {
	if err := entry.check(); err != nil {
		return nil, err
	}
	data, err := get(ctx, client, entry.URL, entry.Bytes)
	if err != nil {
		return nil, fmt.Errorf("download %s: %w", entry.File, err)
	}
	if int64(len(data)) != entry.Bytes {
		return nil, fmt.Errorf("%s is %d bytes, and the catalog says %d", entry.File, len(data), entry.Bytes)
	}
	sum := sha256.Sum256(data)
	if !strings.EqualFold(hex.EncodeToString(sum[:]), entry.SHA256) {
		return nil, fmt.Errorf("%s is not the file the catalog names: its SHA-256 differs", entry.File)
	}
	return data, nil
}

// get reads at most limit bytes, and fails on one more.
func get(ctx context.Context, client *http.Client, address string, limit int64) ([]byte, error) {
	ctx, cancel := context.WithTimeout(ctx, Timeout)
	defer cancel()
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, address, nil)
	if err != nil {
		return nil, fmt.Errorf("make the request: %w", err)
	}
	response, err := client.Do(request)
	if err != nil {
		return nil, fmt.Errorf("ask %s: %w", address, err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("%s answered %s", address, response.Status)
	}
	data, err := io.ReadAll(io.LimitReader(response.Body, limit+1))
	if err != nil {
		return nil, fmt.Errorf("read the answer: %w", err)
	}
	if int64(len(data)) > limit {
		return nil, fmt.Errorf("the answer is over %d bytes", limit)
	}
	return data, nil
}

// versionParts is a.b.c.n as four numbers, or nil.
func versionParts(version string) []int {
	fields := strings.Split(version, ".")
	if len(fields) != 4 {
		return nil
	}
	parts := make([]int, 4)
	for i, field := range fields {
		number, err := strconv.Atoi(field)
		if err != nil || number < 0 {
			return nil
		}
		parts[i] = number
	}
	return parts
}

func compareVersions(a, b string) int {
	return slices.Compare(versionParts(a), versionParts(b))
}

// Newer says whether version a is newer than b. A version that is not four numbers is older than any.
func Newer(a, b string) bool {
	return versionParts(a) != nil && compareVersions(a, b) > 0
}

// DevSuffix marks a Dev build's version, such as 1.0.0.27-dev. Releases never carry it.
const DevSuffix = "-dev"

// Offer is the newest release in the catalog, when it is newer than the version a garland runs. A Dev
// build counts as its number, so the release of that number is not offered to it. A version that is
// not one of ours is offered nothing, as nothing can be said of it.
func Offer(read Catalog, running string) (Entry, bool) {
	running = strings.TrimSuffix(running, DevSuffix)
	if versionParts(running) == nil || len(read.Firmware) == 0 || !Newer(read.Firmware[0].Version, running) {
		return Entry{}, false
	}
	return read.Firmware[0], true
}
