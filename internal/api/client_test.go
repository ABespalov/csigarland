// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of the API client against a server standing in for a garland.
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
	"encoding/binary"
	"encoding/json"
	"encoding/pem"
	"errors"
	"io"
	"log"
	"math/big"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

const testToken = "abcdef0123"

func serverRoot(t *testing.T, server *httptest.Server) []byte {
	t.Helper()
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: server.Certificate().Raw})
}

func address(server *httptest.Server) string {
	return strings.TrimPrefix(server.URL, "https://")
}

func newGarland(t *testing.T, handler http.HandlerFunc) (*httptest.Server, *Client) {
	t.Helper()
	server := httptest.NewTLSServer(handler)
	t.Cleanup(server.Close)
	client, err := New(address(server), serverRoot(t, server), testToken)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(client.Close)
	return server, client
}

func TestBaseURLTakesWhatAnOwnerTypes(t *testing.T) {
	for input, want := range map[string]string{
		"192.168.1.50":             "https://192.168.1.50",
		"garland.local":            "https://garland.local",
		"garland.example.com:8443": "https://garland.example.com:8443",
		" https://garland.local/ ": "https://garland.local",
		"http://192.168.1.50":      "https://192.168.1.50",
	} {
		got, err := BaseURL(input)
		if err != nil || got != want {
			t.Errorf("BaseURL(%q) = %q, %v; want %q", input, got, err, want)
		}
	}
	for _, input := range []string{"", "   ", "garland.local/path", "me@garland", "a b"} {
		if _, err := BaseURL(input); err == nil {
			t.Errorf("BaseURL(%q) took it", input)
		}
	}
}

func TestFirstContactFetchesTheRootToBeCompared(t *testing.T) {
	server := httptest.NewUnstartedServer(nil)
	server.StartTLS()
	defer server.Close()
	root := serverRoot(t, server)
	server.Config.Handler = http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != rootPath {
			t.Errorf("asked for %s", r.URL.Path)
		}
		_, _ = w.Write(root)
	})
	got, err := FetchRoot(context.Background(), address(server))
	if err != nil {
		t.Fatal(err)
	}
	parsed, err := ParseRoot(root)
	if err != nil {
		t.Fatal(err)
	}
	if got.Fingerprint != parsed.Fingerprint || len(got.Fingerprint) != 64 || got.Fingerprint != strings.ToUpper(got.Fingerprint) {
		t.Errorf("fingerprint %q", got.Fingerprint)
	}
}

func TestOnlyTheKeptRootIsTrusted(t *testing.T) {
	garland, client := newGarland(t, func(w http.ResponseWriter, _ *http.Request) {
		_, _ = w.Write([]byte(`{"api":1}`))
	})
	if _, err := client.About(context.Background()); err != nil {
		t.Fatalf("its own root: %v", err)
	}
	if _, err := New(address(garland), []byte("not a certificate"), ""); err == nil {
		t.Fatal("a root that cannot be read was taken")
	}

	// Someone else's certificate at the address, for any name: the chain is what decides.
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	template := &x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject:      pkix.Name{CommonName: "not the garland"},
		NotBefore:    time.Now().Add(-time.Hour),
		NotAfter:     time.Now().Add(time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		IPAddresses:  []net.IP{net.IPv4(127, 0, 0, 1)},
	}
	raw, err := x509.CreateCertificate(rand.Reader, template, template, &key.PublicKey, key)
	if err != nil {
		t.Fatal(err)
	}
	other := httptest.NewUnstartedServer(http.HandlerFunc(func(http.ResponseWriter, *http.Request) {
		t.Error("the request reached a server with another root")
	}))
	other.TLS = &tls.Config{Certificates: []tls.Certificate{{Certificate: [][]byte{raw}, PrivateKey: key}}}
	other.Config.ErrorLog = log.New(io.Discard, "", 0)
	other.StartTLS()
	defer other.Close()
	pinned, err := New(address(other), serverRoot(t, garland), testToken)
	if err != nil {
		t.Fatal(err)
	}
	defer pinned.Close()
	if _, err := pinned.About(context.Background()); !errors.Is(err, ErrWrongRoot) {
		t.Fatalf("another root: %v", err)
	}
}

func TestTheTokenGoesInItsHeader(t *testing.T) {
	var seen string
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		seen = r.Header.Get("Authorization")
		_, _ = w.Write([]byte(`{}`))
	})
	if _, err := client.State(context.Background()); err != nil {
		t.Fatal(err)
	}
	if seen != "Bearer "+testToken {
		t.Errorf("Authorization %q", seen)
	}
	if _, err := client.WithToken("").About(context.Background()); err != nil {
		t.Fatal(err)
	}
	if seen != "" {
		t.Errorf("no token, and Authorization %q", seen)
	}
}

func TestAFailureIsReadWhole(t *testing.T) {
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/api/v1/state":
			w.WriteHeader(http.StatusUnauthorized)
			_, _ = w.Write([]byte(`{"error":{"code":"not_paired","message":"this browser is not paired"}}`))
		case "/api/v1/pair/request":
			w.Header().Set("Retry-After", "42")
			w.WriteHeader(http.StatusTooManyRequests)
			_, _ = w.Write([]byte(`{"error":{"code":"try_later","message":"wait"}}`))
		default:
			w.WriteHeader(http.StatusConflict)
			_, _ = w.Write([]byte(`plain words`))
		}
	})
	_, err := client.State(context.Background())
	var failure *Error
	if !errors.As(err, &failure) || failure.Code != "not_paired" || failure.Message != "this browser is not paired" || !IsNotPaired(err) {
		t.Errorf("not paired: %#v", err)
	}
	_, err = client.RequestPairing(context.Background(), "test")
	if !errors.As(err, &failure) || failure.RetryAfter != 42*time.Second {
		t.Errorf("retry: %#v", err)
	}
	_, err = client.SetSchedule(context.Background(), nil, 1)
	if !IsConflict(err) || err.Error() != "plain words" {
		t.Errorf("conflict: %v", err)
	}
}

func TestRequestsCarryWhatTheFirmwareReads(t *testing.T) {
	type call struct {
		method, path, query string
		body                map[string]any
	}
	var last call
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		last = call{method: r.Method, path: r.URL.Path, query: r.URL.RawQuery}
		data, _ := io.ReadAll(r.Body)
		last.body = nil
		_ = json.Unmarshal(data, &last.body)
		_, _ = w.Write([]byte(`{}`))
	})
	ctx := context.Background()
	check := func(name, method, path string, field string, want any, err error) {
		t.Helper()
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if last.method != method || last.path != "/api/v1/"+path {
			t.Errorf("%s: %s %s", name, last.method, last.path)
		}
		if field != "" && last.body[field] != want {
			t.Errorf("%s: %s = %v, want %v", name, field, last.body[field], want)
		}
	}
	_, err := client.SetBrightness(ctx, 40)
	check("brightness", "POST", "state", "brightness", 40.0, err)
	_, err = client.SetSpeed(ctx, 1.25)
	check("speed", "POST", "state", "speed", 1.25, err)
	_, err = client.SetPower(ctx, false)
	check("power", "POST", "power", "on", false, err)
	_, err = client.SetLeds(ctx, 32)
	check("leds", "POST", "leds", "count", 32.0, err)
	check("play", "POST", "effects/play", "id", "fire", client.Play(ctx, "fire"))
	check("favorite", "POST", "effects/favorite", "favorite", true, client.SetFavorite(ctx, "fire", true))
	_, err = client.SetZone(ctx, "UTC0")
	check("zone", "POST", "time", "zone", "UTC0", err)
	_, err = client.SetModuleByHand(ctx, "2026-10-31T18:05")
	check("by hand", "POST", "time/rtc", "local", "2026-10-31T18:05", err)
	_, err = client.SetModuleFromInternet(ctx)
	check("internet", "POST", "time/rtc", "source", "ntp", err)
	_, err = client.JoinWifi(ctx, "Home", "secret")
	check("wifi", "POST", "wifi", "ssid", "Home", err)
	check("name", "POST", "network/name", "name", "tree", client.SetName(ctx, "tree"))
	check("domain", "POST", "network/certificate", "domain", "a.example", client.SetDomain(ctx, "a.example"))
	_, err = client.ForgetClient(ctx, 3)
	check("forget", "POST", "clients/forget", "id", 3.0, err)
	_, err = client.WaitPairing(ctx, "abc")
	check("wait", "POST", "pair/wait", "request", "abc", err)
	check("delete", "DELETE", "effects/file", "", nil, client.DeleteEffect(ctx, "mine"))
	if last.query != "id=mine" {
		t.Errorf("delete query %q", last.query)
	}
	check("save", "POST", "effects/file", "format", "x", client.SaveEffect(ctx, "mine", []byte(`{"format":"x"}`)))
	check("preview", "POST", "effects/preview", "format", "x", client.PreviewEffect(ctx, []byte(`{"format":"x"}`)))
}

func TestAScheduleKeepsToWhatTheFormNames(t *testing.T) {
	var sent struct {
		Entries  []map[string]any `json:"entries"`
		Revision uint32           `json:"revision"`
	}
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		_ = json.NewDecoder(r.Body).Decode(&sent)
		_, _ = w.Write([]byte(`{"entries":[],"max":32,"revision":9,"next":{}}`))
	})
	answer, err := client.SetSchedule(context.Background(), []Action{{Enabled: true, Time: "23:30", Days: 127, Power: StepOff}}, 8)
	if err != nil {
		t.Fatal(err)
	}
	if sent.Revision != 8 || answer.Revision != 9 {
		t.Errorf("revisions %d, %d", sent.Revision, answer.Revision)
	}
	entry := sent.Entries[0]
	if _, named := entry["brightness"]; named {
		t.Error("a step nobody asked for was sent")
	}
	if entry["power"] != "off" || entry["time"] != "23:30" || entry["enabled"] != true {
		t.Errorf("entry %v", entry)
	}
}

func TestTheLogIsReadFromAPosition(t *testing.T) {
	var query string
	_, client := newGarland(t, func(w http.ResponseWriter, r *http.Request) {
		query = r.URL.RawQuery
		w.Header().Set("X-Log-Position", "300")
		w.Header().Set("X-Log-Lost", "12")
		_, _ = w.Write([]byte("I (1) line\n"))
	})
	part, err := client.Log(context.Background(), 200)
	if err != nil {
		t.Fatal(err)
	}
	if query != "after=200" || part.Position != 300 || part.Lost != 12 || part.Text != "I (1) line\n" {
		t.Errorf("%q, %+v", query, part)
	}
}

func makeUpdate(pack, image []byte) []byte {
	const certificate = 10
	file := make([]byte, updateHeaderBytes+certificate)
	copy(file, updateMagic)
	binary.LittleEndian.PutUint32(file[4:], updateFormat)
	binary.LittleEndian.PutUint32(file[8:], certificate)
	binary.LittleEndian.PutUint32(file[12:], uint32(len(pack)))
	binary.LittleEndian.PutUint32(file[16:], uint32(len(image)))
	for i := 0; i < updateDigestBytes; i++ {
		file[updateDigestOffset+i] = 0xAB
	}
	return append(append(file, pack...), image...)
}

func makePack(records ...string) []byte {
	pack := make([]byte, packHeaderSize)
	copy(pack, "CGPK")
	binary.LittleEndian.PutUint32(pack[4:], packFormat)
	for _, record := range records {
		pack = append(pack, record...)
	}
	binary.LittleEndian.PutUint32(pack[8:], uint32(len(pack)))
	return pack
}

func makeImage(version string) []byte {
	image := make([]byte, imageDescOffset+imageDescBytes+10)
	desc := image[imageDescOffset:]
	binary.LittleEndian.PutUint32(desc, imageDescMagic)
	copy(desc[imageVersionAt:], version)
	for i := 0; i < imageHashBytes; i++ {
		desc[imageHashAt+i] = 0xCD
	}
	return image
}

func TestAnUpdateFilesOddPartsAreSaidToBe(t *testing.T) {
	noCertificate := makeUpdate(makePack("eid\x00G\x00N\x00{}\x00"), nil)
	binary.LittleEndian.PutUint32(noCertificate[8:], 0)
	if _, err := ParseUpdate(noCertificate); err == nil {
		t.Error("no certificate, and it was taken")
	}
	for name, pack := range map[string][]byte{
		"cut short":  makePack("eid\x00G\x00N"),
		"not a pack": append([]byte("XXXX"), makePack("eid\x00G\x00N\x00{}\x00")[4:]...),
		"wrong size": func() []byte {
			pack := makePack("eid\x00G\x00N\x00{}\x00")
			binary.LittleEndian.PutUint32(pack[8:], 1)
			return pack
		}(),
	} {
		update, err := ParseUpdate(makeUpdate(pack, nil))
		if err != nil || update.Effects != -1 {
			t.Errorf("%s: %+v, %v", name, update, err)
		}
	}
}

func TestAnUpdateFileSaysWhatItHolds(t *testing.T) {
	pack := makePack("eid\x00G\x00N\x00{}\x00", "fid2\x00G\x00N\x00{}\x00", "pp\x00G\x00N\x00\x00")
	update, err := ParseUpdate(makeUpdate(pack, makeImage("1.0.0.9")))
	if err != nil {
		t.Fatal(err)
	}
	if !update.HasFirmware || update.Version != "1.0.0.9" || !strings.HasPrefix(update.Image, "cdcd") {
		t.Errorf("firmware: %+v", update)
	}
	if !update.HasEffects || update.Effects != 2 || !strings.HasPrefix(update.PackDigest, "abab") {
		t.Errorf("effects: %+v", update)
	}
	only, err := ParseUpdate(makeUpdate(nil, makeImage("v")))
	if err != nil || only.HasEffects || !only.HasFirmware {
		t.Errorf("firmware alone: %+v, %v", only, err)
	}

	for name, file := range map[string][]byte{
		"short":  []byte("CGOT"),
		"magic":  append([]byte("XXXX"), make([]byte, updateHeaderBytes)...),
		"cut":    makeUpdate(pack, nil)[:updateHeaderBytes+5],
		"empty":  makeUpdate(nil, nil),
		"format": func() []byte { f := makeUpdate(pack, nil); binary.LittleEndian.PutUint32(f[4:], 99); return f }(),
	} {
		if _, err := ParseUpdate(file); err == nil {
			t.Errorf("%s: taken", name)
		}
	}
}

func TestAnUpdateIsJudgedByWhatRuns(t *testing.T) {
	update := UpdateFile{Image: "new", PackDigest: "pack"}
	for name, c := range map[string]struct {
		now  Firmware
		want UpdateStage
	}{
		"same boot": {Firmware{Boot: "old", Image: "new", PackDigest: "pack"}, UpdateNotRestarted},
		"went back": {Firmware{Boot: "b", Image: "old", PackDigest: "pack"}, UpdateWentBack},
		"old pack":  {Firmware{Boot: "b", Image: "new", PackDigest: "old"}, UpdateWrongEffects},
		"on trial":  {Firmware{Boot: "b", Image: "new", PackDigest: "pack", OnTrial: true}, UpdateOnTrial},
		"confirmed": {Firmware{Boot: "b", Image: "new", PackDigest: "pack"}, UpdateConfirmed},
	} {
		if got := JudgeUpdate(update, "old", c.now); got != c.want {
			t.Errorf("%s: %v, want %v", name, got, c.want)
		}
	}
}
