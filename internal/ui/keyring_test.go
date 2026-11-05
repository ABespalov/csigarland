// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Holds the token reads away from the window to the owner's latest choice, whatever came between.
// Contributors: None

package ui

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"encoding/pem"
	"math/big"
	"path/filepath"
	"sync"
	"testing"
	"time"

	"github.com/ABespalov/csigarland/internal/settings"
)

// slowStore stands in for a keyring that answers each read only when told to, as a locked one waits for
// its password.
type slowStore struct {
	mu       sync.Mutex
	started  map[string]chan struct{}
	release  map[string]chan struct{}
	released map[string]bool
	deleting chan struct{} // A forgetting begun, which waits until the test ends.
}

func newSlowStore(t *testing.T, fingerprints ...string) *slowStore {
	t.Helper()
	store := &slowStore{started: map[string]chan struct{}{}, release: map[string]chan struct{}{}, released: map[string]bool{},
		deleting: make(chan struct{}, len(fingerprints)+1)}
	for _, fingerprint := range fingerprints {
		store.started[fingerprint] = make(chan struct{})
		store.release[fingerprint] = make(chan struct{})
	}
	savedGet, savedDelete := tokenGet, tokenDelete
	t.Cleanup(func() { tokenGet, tokenDelete = savedGet, savedDelete })
	tokenGet = func(fingerprint string) (string, error) {
		store.mu.Lock()
		started, release := store.started[fingerprint], store.release[fingerprint]
		store.mu.Unlock()
		close(started)
		<-release
		return "token-" + fingerprint, nil
	}
	// Forgetting waits until the test ends, so that what it posts never stands in the queue for a read's answer.
	// At the end every read and forgetting still waiting is let go, before the store is put back, which the
	// cleanups run in that order for.
	deletes := make(chan struct{})
	t.Cleanup(func() {
		close(deletes)
		store.mu.Lock()
		defer store.mu.Unlock()
		for fingerprint, release := range store.release {
			if !store.released[fingerprint] {
				close(release)
			}
		}
	})
	tokenDelete = func(string) error {
		store.deleting <- struct{}{}
		<-deletes
		return nil
	}
	return store
}

// keyringApp is a utility with these garlands remembered and paired, and no network: its lifetime ends
// before any answer is applied, so a connection it started would make no request.
func keyringApp(t *testing.T, fingerprints ...string) *App {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	template := &x509.Certificate{SerialNumber: big.NewInt(1), NotBefore: time.Now(), NotAfter: time.Now().Add(time.Hour)}
	der, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	a := newApp("test")
	t.Cleanup(a.stop)
	a.invalidate = func() {}
	a.settingsPath = filepath.Join(t.TempDir(), "utility.json")
	for i, fingerprint := range fingerprints {
		a.settings.Remember(settings.Garland{Address: "192.0.2." + string(rune('1'+i)), Fingerprint: fingerprint,
			RootPEM: string(pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}))})
	}
	return a
}

// answered waits for the read of one garland's token, then for what it posts to the window.
func answered(t *testing.T, a *App, store *slowStore, fingerprint string) func() {
	t.Helper()
	store.mu.Lock()
	close(store.release[fingerprint])
	store.released[fingerprint] = true
	store.mu.Unlock()
	select {
	case done := <-a.queue:
		return done
	case <-time.After(5 * time.Second):
		t.Fatalf("the read of %s posted nothing", fingerprint)
		return nil
	}
}

func pairedRow(a *App, fingerprint string) GarlandRow {
	garland, _ := a.settings.Find(fingerprint)
	return GarlandRow{Fingerprint: fingerprint, Address: garland.Address, Paired: true}
}

// A garland picked, then forgotten while its token was read, stays forgotten when the token comes.
func TestAGarlandForgottenWhileItsTokenIsReadStaysForgotten(t *testing.T) {
	a := keyringApp(t, "AA")
	store := newSlowStore(t, "AA")
	picked := pairedRow(a, "AA")
	a.connect.pick(a, picked)
	<-store.started["AA"]
	a.connect.forgetRow(a, picked)
	<-store.deleting
	done := answered(t, a, store, "AA")
	a.stop()
	done()
	if _, kept := a.settings.Find("AA"); kept || a.connected {
		t.Fatalf("a forgotten garland came back: kept %v, connected %v", kept, a.connected)
	}
}

// Of two garlands picked in turn, the token of the first, coming late, does not connect to it.
func TestTheGarlandPickedLastIsTheOneConnectedTo(t *testing.T) {
	a := keyringApp(t, "AA", "BB")
	store := newSlowStore(t, "AA", "BB")
	a.connect.pick(a, pairedRow(a, "AA"))
	<-store.started["AA"]
	a.connect.pick(a, pairedRow(a, "BB"))
	<-store.started["BB"]
	done := answered(t, a, store, "AA")
	a.stop()
	done()
	if a.connected {
		t.Fatalf("the garland picked first was connected to: %q", a.current.Fingerprint)
	}
}

// The connection a start makes to the garland used last gives way to one the owner picked meanwhile.
func TestAStartsConnectionGivesWayToThePick(t *testing.T) {
	a := keyringApp(t, "BB", "AA")
	store := newSlowStore(t, "AA", "BB")
	last, _ := a.settings.Last()
	if last.Fingerprint != "AA" {
		t.Fatalf("the last used is %s", last.Fingerprint)
	}
	a.resumeToken(last, a.connect.choice.Load())
	<-store.started["AA"]
	a.connect.pick(a, pairedRow(a, "BB"))
	<-store.started["BB"]
	done := answered(t, a, store, "AA")
	a.stop()
	done()
	if a.connected {
		t.Fatalf("the start connected to %q over the owner's pick", a.current.Fingerprint)
	}
}
