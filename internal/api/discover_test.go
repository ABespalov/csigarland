// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests for finding garlands: their answers, and that only garlands are kept.
// Contributors: None

package api

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"
	"time"

	"golang.org/x/net/dns/dnsmessage"
)

func recordHead(name string, kind dnsmessage.Type) dnsmessage.ResourceHeader {
	return dnsmessage.ResourceHeader{Name: dnsmessage.MustNewName(name), Type: kind, Class: dnsmessage.ClassINET, TTL: 120}
}

func packAnswer(t *testing.T, answers, additionals []dnsmessage.Resource) []byte {
	t.Helper()
	message := dnsmessage.Message{Header: dnsmessage.Header{Response: true, Authoritative: true}, Answers: answers, Additionals: additionals}
	data, err := message.Pack()
	if err != nil {
		t.Fatal(err)
	}
	return data
}

func ptr(service, instance string) dnsmessage.Resource {
	return dnsmessage.Resource{Header: recordHead(service, dnsmessage.TypePTR), Body: &dnsmessage.PTRResource{PTR: dnsmessage.MustNewName(instance)}}
}

func srv(instance, host string, port uint16) dnsmessage.Resource {
	return dnsmessage.Resource{Header: recordHead(instance, dnsmessage.TypeSRV), Body: &dnsmessage.SRVResource{Port: port, Target: dnsmessage.MustNewName(host)}}
}

func hostAddress(host string, ip [4]byte) dnsmessage.Resource {
	return dnsmessage.Resource{Header: recordHead(host, dnsmessage.TypeA), Body: &dnsmessage.AResource{A: ip}}
}

func discoveryAnswer(t *testing.T, service string, port uint16, ip [4]byte) []byte {
	t.Helper()
	instance := "csigarland." + service
	return packAnswer(t, []dnsmessage.Resource{ptr(service, instance)},
		[]dnsmessage.Resource{srv(instance, "csigarland.local.", port), hostAddress("csigarland.local.", ip)})
}

func TestAnAnswerSaysWhereTheGarlandIs(t *testing.T) {
	source := net.IPv4(192, 0, 2, 9)
	found := parseDiscovery(discoveryAnswer(t, discoveryService, 443, [4]byte{192, 0, 2, 50}), source)
	if len(found) != 1 || found[0].Name != "csigarland.local" || found[0].Address != "192.0.2.50" {
		t.Fatalf("%+v", found)
	}
	found = parseDiscovery(discoveryAnswer(t, discoveryService, 8443, [4]byte{192, 0, 2, 50}), source)
	if len(found) != 1 || found[0].Address != "192.0.2.50:8443" {
		t.Fatalf("%+v", found)
	}
}

func TestAnAnswerAboutSomethingElseIsNoGarland(t *testing.T) {
	if found := parseDiscovery(discoveryAnswer(t, "_printer._tcp.local.", 443, [4]byte{192, 0, 2, 50}), net.IPv4(192, 0, 2, 9)); len(found) != 0 {
		t.Fatalf("%+v", found)
	}
	if found := parseDiscovery([]byte("not DNS at all"), net.IPv4(192, 0, 2, 9)); len(found) != 0 {
		t.Fatalf("%+v", found)
	}
}

func TestAnAnswerIsFollowedFromServiceToHostToAddress(t *testing.T) {
	const instanceA = "a." + discoveryService
	const instanceB = "b." + discoveryService
	data := packAnswer(t,
		[]dnsmessage.Resource{ptr(discoveryService, instanceA), ptr(discoveryService, instanceB), ptr("_printer._tcp.local.", "p._printer._tcp.local.")},
		[]dnsmessage.Resource{
			hostAddress("other.local.", [4]byte{192, 0, 2, 200}),
			srv("p._printer._tcp.local.", "other.local.", 631),
			srv(instanceB, "b.local.", 443),
			hostAddress("b.local.", [4]byte{192, 0, 2, 2}),
			srv(instanceA, "a.local.", 8443),
			hostAddress("a.local.", [4]byte{192, 0, 2, 1}),
			hostAddress("last.local.", [4]byte{192, 0, 2, 201}),
		})
	found := parseDiscovery(data, net.IPv4(192, 0, 2, 9))
	if len(found) != 2 || found[0] != (Found{Name: "a.local", Address: "192.0.2.1:8443"}) || found[1] != (Found{Name: "b.local", Address: "192.0.2.2"}) {
		t.Fatalf("%+v", found)
	}
	// With no address of its own, it is where the answer came from.
	alone := packAnswer(t, []dnsmessage.Resource{ptr(discoveryService, instanceA)}, []dnsmessage.Resource{srv(instanceA, "a.local.", 443)})
	if found := parseDiscovery(alone, net.IPv4(192, 0, 2, 9)); len(found) != 1 || found[0].Address != "192.0.2.9" {
		t.Fatalf("%+v", found)
	}
}

// A whole trip with the network's multicast left out: the question goes to a socket here.
func TestAQuestionIsAnsweredOverASocket(t *testing.T) {
	responder, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer responder.Close()
	answer := discoveryAnswer(t, discoveryService, 443, [4]byte{192, 0, 2, 50})
	go func() {
		buffer := make([]byte, maxPacketBytes)
		length, asker, err := responder.ReadFromUDP(buffer)
		if err != nil {
			return
		}
		var parser dnsmessage.Parser
		if _, err := parser.Start(buffer[:length]); err != nil {
			return
		}
		question, err := parser.Question()
		if err != nil || question.Name.String() != discoveryService || question.Type != dnsmessage.TypePTR {
			return
		}
		_, _ = responder.WriteToUDP(answer, asker)
	}()
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
	defer cancel()
	found := ask(ctx, conn, responder.LocalAddr().(*net.UDPAddr))
	if len(found) != 1 || found[0].Address != "192.0.2.50" {
		t.Fatalf("%+v", found)
	}
}

func TestOnlyWhatSaysItIsAGarlandIsKept(t *testing.T) {
	answering := func(about any) *httptest.Server {
		var server *httptest.Server
		server = httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			switch r.URL.Path {
			case rootPath:
				_, _ = w.Write(serverRoot(t, server))
			case prefix + "about":
				_ = json.NewEncoder(w).Encode(about)
			default:
				http.NotFound(w, r)
			}
		}))
		t.Cleanup(server.Close)
		return server
	}
	garlandServer := answering(About{Product: productName, API: 1, Version: "1.0.0.99"})
	garland := address(garlandServer)
	printer := address(answering(map[string]string{"product": "printer"}))
	older := address(answering(About{Product: productName}))
	found := confirmed(context.Background(), []Found{
		{Name: garland, Address: garland}, {Name: "a", Address: garland},
		{Name: "b", Address: printer}, {Name: "c", Address: older}, {Name: "d", Address: "127.0.0.1:1"},
	})
	if len(found) != 1 || found[0].Name != "a" || found[0].Version != "1.0.0.99" {
		t.Fatalf("%+v", found)
	}
	root, err := ParseRoot(serverRoot(t, garlandServer))
	if err != nil || found[0].Fingerprint != root.Fingerprint {
		t.Fatalf("%q, %v", found[0].Fingerprint, err)
	}
}

func TestAskingIsOneConnectionPerGarland(t *testing.T) {
	var opened atomic.Int32
	var server *httptest.Server
	server = httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == rootPath {
			_, _ = w.Write(serverRoot(t, server))
			return
		}
		_ = json.NewEncoder(w).Encode(About{Product: productName, API: 1})
	}))
	server.Config.ConnState = func(_ net.Conn, state http.ConnState) {
		if state == http.StateNew {
			opened.Add(1)
		}
	}
	server.StartTLS()
	defer server.Close()
	if _, fingerprint, err := probe(context.Background(), address(server)); err != nil || fingerprint == "" {
		t.Fatalf("%q, %v", fingerprint, err)
	}
	if count := opened.Load(); count != 1 {
		t.Fatalf("%d connections", count)
	}
}

func TestWhatIsAskedIsBounded(t *testing.T) {
	var now, most atomic.Int32
	release := make(chan struct{})
	server := httptest.NewUnstartedServer(http.HandlerFunc(func(http.ResponseWriter, *http.Request) {
		count := now.Add(1)
		defer now.Add(-1)
		for {
			seen := most.Load()
			if count <= seen || most.CompareAndSwap(seen, count) {
				break
			}
		}
		<-release
	}))
	// On every loopback address, so that each candidate below is another one and all reach it.
	listener, err := net.Listen("tcp4", "0.0.0.0:0")
	if err != nil {
		t.Fatal(err)
	}
	server.Listener.Close()
	server.Listener = listener
	server.StartTLS()
	defer server.Close()
	defer close(release)
	port := listener.Addr().(*net.TCPAddr).Port
	heard := make([]Found, maxCandidates*3)
	for i := range heard {
		heard[i] = Found{Name: fmt.Sprint(i), Address: fmt.Sprintf("127.0.0.%d:%d", i+1, port)}
	}
	if kept := candidates(heard); len(kept) != maxCandidates {
		t.Fatalf("%d kept", len(kept))
	}
	ctx, cancel := context.WithTimeout(context.Background(), 300*time.Millisecond)
	defer cancel()
	started := time.Now()
	confirmed(ctx, heard)
	if limit := most.Load(); limit > maxConfirming || limit == 0 {
		t.Fatalf("%d at once", limit)
	}
	// It ends with the caller, not with its own longer limit.
	if took := time.Since(started); took > confirmWait/2 {
		t.Fatalf("took %v", took)
	}
}
