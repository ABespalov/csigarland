// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The client against what the firmware really makes and says: its chain and its field names.
// Contributors: None

package api

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/asn1"
	"encoding/json"
	"encoding/pem"
	"errors"
	"io"
	"log"
	"math/big"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
	"time"
)

// What tls_name_constraints_der makes with no domain, which a firmware host test holds to these bytes.
var firmwareConstraints = []byte{
	0x30, 0x17, 0xA0, 0x15, 0x30, 0x07, 0x82, 0x05, 'l', 'o', 'c', 'a', 'l',
	0x30, 0x0A, 0x87, 0x08, 0xC0, 0xA8, 0x04, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
}

const setupAddress = "192.168.4.1"

type firmwareChain struct {
	rootPEM []byte
	root    *x509.Certificate
	key     *ecdsa.PrivateKey
}

// Built as portal_tls.cpp builds it: critical constraints on a root, on a P-256 key.
func newFirmwareChain(t *testing.T) firmwareChain {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	template := &x509.Certificate{
		SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "csiGarland root"},
		NotBefore: time.Now().Add(-time.Hour), NotAfter: time.Now().Add(time.Hour),
		IsCA: true, BasicConstraintsValid: true, KeyUsage: x509.KeyUsageCertSign | x509.KeyUsageCRLSign,
		ExtraExtensions: []pkix.Extension{{Id: asn1.ObjectIdentifier{2, 5, 29, 30}, Critical: true, Value: firmwareConstraints}},
	}
	der, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	root, err := x509.ParseCertificate(der)
	if err != nil {
		t.Fatal(err)
	}
	return firmwareChain{rootPEM: pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), root: root, key: key}
}

func (c firmwareChain) serve(t *testing.T, names ...string) *httptest.Server {
	t.Helper()
	return c.serveDated(t, time.Now().Add(-time.Hour), time.Now().Add(time.Hour), names...)
}

func (c firmwareChain) serveDated(t *testing.T, notBefore, notAfter time.Time, names ...string) *httptest.Server {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	template := &x509.Certificate{
		SerialNumber: big.NewInt(2), Subject: pkix.Name{CommonName: "csiGarland"},
		NotBefore: notBefore, NotAfter: notAfter,
		KeyUsage: x509.KeyUsageDigitalSignature, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		DNSNames: names, IPAddresses: []net.IP{net.ParseIP(setupAddress)},
	}
	der, err := x509.CreateCertificate(rand.Reader, template, c.root, &key.PublicKey, c.key)
	if err != nil {
		t.Fatal(err)
	}
	server := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		_, _ = w.Write([]byte(`{"api":1}`))
	}))
	server.Config.ErrorLog = log.New(io.Discard, "", 0)
	server.TLS = &tls.Config{Certificates: []tls.Certificate{{Certificate: [][]byte{der}, PrivateKey: key}}}
	server.StartTLS()
	t.Cleanup(server.Close)
	return server
}

func TestAChainAsTheFirmwareMakesItIsTaken(t *testing.T) {
	chain := newFirmwareChain(t)
	server := chain.serve(t, "csigarland.local")
	client, err := New(address(server), chain.rootPEM, testToken)
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	about, err := client.About(context.Background())
	if err != nil || about.API != 1 {
		t.Fatalf("%+v, %v", about, err)
	}
}

// As after the owner sets a domain: the firmware makes a new root for it, and the one kept here
// does not allow the new name, so the utility has to connect again.
func TestANameTheKeptRootDoesNotAllowIsRefused(t *testing.T) {
	chain := newFirmwareChain(t)
	server := chain.serve(t, "csigarland.local", "garland.example.com")
	client, err := New(address(server), chain.rootPEM, testToken)
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	if _, err := client.About(context.Background()); !errors.Is(err, ErrWrongRoot) {
		t.Fatalf("%v", err)
	}
}

func TestOnlyTheRootWhoseFingerprintWasShownIsTrusted(t *testing.T) {
	real := newFirmwareChain(t)
	other := newFirmwareChain(t)
	file := append(append([]byte(nil), real.rootPEM...), other.rootPEM...)
	root, err := ParseRoot(file)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Count(string(root.PEM), "BEGIN CERTIFICATE") != 1 {
		t.Fatalf("%s", root.PEM)
	}
	server := other.serve(t, "csigarland.local")
	client, err := New(address(server), file, testToken)
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	if _, err := client.About(context.Background()); !errors.Is(err, ErrWrongRoot) {
		t.Fatalf("the second certificate of the file was trusted: %v", err)
	}
}

// A clock wrong here or on the garland is no other garland: pairing again would not mend it.
func TestACertificateOutOfItsDatesIsNotAnotherRoot(t *testing.T) {
	real := newFirmwareChain(t)
	other := newFirmwareChain(t)
	expired := func(chain firmwareChain) error {
		server := chain.serveDated(t, time.Now().Add(-50*time.Minute), time.Now().Add(-time.Minute), "csigarland.local")
		client, err := New(address(server), real.rootPEM, testToken)
		if err != nil {
			t.Fatal(err)
		}
		defer client.Close()
		_, err = client.About(context.Background())
		return err
	}
	if err := expired(real); !errors.Is(err, ErrCertificateDates) || errors.Is(err, ErrWrongRoot) {
		t.Fatalf("its own root, out of date: %v", err)
	}
	if err := expired(other); !errors.Is(err, ErrWrongRoot) || errors.Is(err, ErrCertificateDates) {
		t.Fatalf("another root, out of date: %v", err)
	}
}

func TestAnUploadThatStopsMovingEnds(t *testing.T) {
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		_, _ = io.Copy(io.Discard, r.Body)
		<-r.Context().Done()
	})
	client.stall = 100 * time.Millisecond
	if err := client.Install(context.Background(), []byte("update"), nil); !errors.Is(err, ErrInstallStalled) {
		t.Fatalf("%v", err)
	}
}

func TestAnUploadArrivesWholeAndWithItsLength(t *testing.T) {
	var got [][]byte
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		data, _ := io.ReadAll(r.Body)
		got = append(got, data)
		if r.ContentLength != 6 {
			t.Errorf("length %d, and the garland needs it", r.ContentLength)
		}
	})
	var sent, total int64
	if err := client.Install(context.Background(), []byte("update"), func(s, t int64) { sent, total = s, t }); err != nil {
		t.Fatal(err)
	}
	if len(got) != 1 || string(got[0]) != "update" {
		t.Fatalf("%q", got)
	}
	if sent != 6 || total != 6 {
		t.Fatalf("told %d of %d", sent, total)
	}
}

// The garland may have taken all of it and be about to restart. A cancelled wait says nothing of that,
// and has to look like what it is: no answer, rather than a refusal.
func TestCancellingAfterTheGarlandTookAnUpdateIsNoRefusal(t *testing.T) {
	took := make(chan struct{})
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		_, _ = io.Copy(io.Discard, r.Body)
		close(took)
		<-r.Context().Done()
	})
	ctx, cancel := context.WithCancel(context.Background())
	go func() {
		<-took
		cancel()
	}()
	err := client.Install(ctx, []byte("update"), nil)
	var refused *Error
	if !errors.Is(err, context.Canceled) || errors.As(err, &refused) {
		t.Fatalf("%v", err)
	}
}

func TestOnlyWhatSetsAValueIsSentTwice(t *testing.T) {
	keys := map[string]string{}
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		keys[r.URL.Path] = r.Header.Get(idempotencyKey)
		_, _ = w.Write([]byte(`{}`))
	})
	ctx := context.Background()
	if err := client.Play(ctx, "aurora"); err != nil {
		t.Fatal(err)
	}
	if _, err := client.RequestPairing(ctx, "x"); err != nil {
		t.Fatal(err)
	}
	if _, err := client.WaitPairing(ctx, "x"); err != nil {
		t.Fatal(err)
	}
	if keys[prefix+"effects/play"] == "" || keys[prefix+"pair/request"] != "" || keys[prefix+"pair/wait"] != "" {
		t.Fatalf("%v", keys)
	}
	for _, route := range []string{"schedule", "pair/request", "pair/wait", "wifi", "network/name", "network/certificate", "clients/forget", "effects/file", "time/rtc", "wifi/scan"} {
		if safeToSendTwice(http.MethodPost, route) {
			t.Errorf("%s would be sent twice", route)
		}
	}
	if safeToSendTwice(http.MethodDelete, "effects/file") {
		t.Error("a delete would be sent twice")
	}
}

func TestAnEmptyScheduleIsAList(t *testing.T) {
	var sent map[string]json.RawMessage
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewDecoder(r.Body).Decode(&sent)
		_, _ = w.Write([]byte(`{"entries":[],"revision":4}`))
	})
	if _, err := client.SetSchedule(context.Background(), nil, 3); err != nil {
		t.Fatal(err)
	}
	if string(sent["entries"]) != "[]" {
		t.Fatalf("%s: the garland refuses anything but a list", sent["entries"])
	}
}

func TestAnAddressHasAPortThatCanBe(t *testing.T) {
	for _, address := range []string{"garland.local:0", "garland.local:65536", "garland.local:99999"} {
		if cleaned, err := CleanAddress(address); err == nil {
			t.Errorf("%s became %s", address, cleaned)
		}
	}
	for _, address := range []string{"garland.local:1", "garland.local:65535"} {
		if _, err := CleanAddress(address); err != nil {
			t.Errorf("%s: %v", address, err)
		}
	}
}

func TestAnAddressHasOneSpelling(t *testing.T) {
	for typed, want := range map[string]string{
		"192.0.2.50":                 "192.0.2.50",
		" https://192.0.2.50/ ":      "192.0.2.50",
		"HTTPS://Garland.Local:443/": "garland.local",
		"http://garland.local":       "garland.local",
		"garland.local:8443":         "garland.local:8443",
		"[2001:db8::1]:443":          "[2001:db8::1]",
		"[2001:db8::1]:8443":         "[2001:db8::1]:8443",
	} {
		if got, err := CleanAddress(typed); err != nil || got != want {
			t.Errorf("%q: %q, %v", typed, got, err)
		}
	}
	for _, bad := range []string{"", "  ", "garland.local:badport", "a/b", "user@garland", "https://"} {
		if got, err := CleanAddress(bad); err == nil {
			t.Errorf("%q: taken as %q", bad, got)
		}
	}
}

func TestAWifiPasswordStaysAsShortAsItIs(t *testing.T) {
	password := strings.Repeat("&<>", 21)
	data, err := marshal(map[string]string{"password": password})
	if err != nil {
		t.Fatal(err)
	}
	if want := `{"password":"` + password + `"}`; string(data) != want {
		t.Fatalf("%s", data)
	}
}

// The handlers run only on a board, so at least a name that left the firmware is caught here.
func TestEveryFieldIsOneTheFirmwareNames(t *testing.T) {
	var source strings.Builder
	files, err := filepath.Glob(filepath.Join("..", "..", "firmware", "app", "src", "*.*"))
	if err != nil || len(files) == 0 {
		t.Fatalf("%d files, %v", len(files), err)
	}
	for _, file := range files {
		data, err := os.ReadFile(file)
		if err != nil {
			t.Fatal(err)
		}
		source.Write(data)
	}
	text := source.String()
	shapes := []any{About{}, PairRequest{}, PairAnswer{}, Leds{}, State{}, Effect{}, EffectList{}, Action{}, NextAction{}, Schedule{}, ClockModule{},
		Clock{}, Display{}, Wifi{}, Network{}, Scan{}, Firmware{}, Info{}, PairedClient{}, clientList{}}
	for _, shape := range shapes {
		kind := reflect.TypeOf(shape)
		for i := range kind.NumField() {
			name, _, _ := strings.Cut(kind.Field(i).Tag.Get("json"), ",")
			if !strings.Contains(text, `"`+name+`"`) && !strings.Contains(text, `\"`+name+`\"`) {
				t.Errorf("%s.%s: the firmware never says %q", kind.Name(), kind.Field(i).Name, name)
			}
		}
	}
}
