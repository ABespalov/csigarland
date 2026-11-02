// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Finds the garlands on the local network, by mDNS, and asks each what it is.
// Contributors: None

package api

import (
	"context"
	"crypto/tls"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"slices"
	"strconv"
	"strings"
	"sync"
	"time"

	"golang.org/x/net/dns/dnsmessage"
	"golang.org/x/net/ipv4"
)

const (
	// What the firmware announces itself under, in network.cpp.
	discoveryService = "_csigarland._tcp.local."
	discoveryAddress = "224.0.0.251:5353"
	discoveryTTL     = 255
	askUnicast       = 0x8000 // The top bit of a question's class: answer the asker directly.
	defaultPort      = 443
	maxPacketBytes   = 1500
	productName      = "csiGarland"
	// DiscoveryWait is how long to listen for garlands' answers.
	DiscoveryWait = 3 * time.Second
	// Anyone on the network can answer, with as many addresses as they like, so what is asked after
	// that is bounded: how many, how many at a time, and how long in all.
	maxCandidates  = 16
	maxConfirming  = 4
	confirmWait    = 8 * time.Second
	confirmConnect = 3 * time.Second
)

// Found is a garland that answered on the network. Nothing here is proof of anything: it is only what
// to offer the owner, who still compares the root's fingerprint before trusting it.
type Found struct {
	Name    string // Its mDNS name.
	Address string // What to connect to.
	Version string // Its firmware.
	// Of the root it presents: what tells a garland met before, whatever its address now. It is not
	// proof either: a client with this root kept still has it checked as it connects.
	Fingerprint string
}

// Discover looks for garlands on every network this computer is on, listening for as long as given,
// and keeps those that say over HTTPS that they are one. All of it ends with ctx.
func Discover(ctx context.Context, listen time.Duration) ([]Found, error) {
	interfaces, err := net.Interfaces()
	if err != nil {
		return nil, fmt.Errorf("list the network interfaces: %w", err)
	}
	destination, err := net.ResolveUDPAddr("udp4", discoveryAddress)
	if err != nil {
		return nil, fmt.Errorf("the mDNS address: %w", err)
	}
	listening, cancel := context.WithTimeout(ctx, listen)
	defer cancel()
	var mutex sync.Mutex
	var heard []Found
	var waiting sync.WaitGroup
	for _, card := range interfaces {
		if card.Flags&net.FlagUp == 0 || card.Flags&net.FlagMulticast == 0 {
			continue
		}
		addresses, err := card.Addrs()
		if err != nil {
			continue
		}
		for _, address := range addresses {
			ip, isIP := address.(*net.IPNet)
			if !isIP || ip.IP.To4() == nil || ip.IP.IsLoopback() {
				continue
			}
			conn, err := net.ListenUDP("udp4", &net.UDPAddr{IP: ip.IP})
			if err != nil {
				continue
			}
			packets := ipv4.NewPacketConn(conn)
			if packets.SetMulticastInterface(&card) != nil || packets.SetMulticastTTL(discoveryTTL) != nil {
				conn.Close()
				continue
			}
			waiting.Add(1)
			go func() {
				defer waiting.Done()
				found := ask(listening, conn, destination)
				mutex.Lock()
				heard = append(heard, found...)
				mutex.Unlock()
			}()
		}
	}
	waiting.Wait()
	return confirmed(ctx, heard), nil
}

// ask sends the question on one socket, closes it when ctx ends, and gathers what comes back.
func ask(ctx context.Context, conn *net.UDPConn, destination *net.UDPAddr) []Found {
	stop := context.AfterFunc(ctx, func() { conn.Close() })
	defer stop()
	defer conn.Close()
	query, err := discoveryQuery()
	if err != nil {
		return nil
	}
	if _, err := conn.WriteToUDP(query, destination); err != nil {
		return nil
	}
	var found []Found
	buffer := make([]byte, maxPacketBytes)
	for {
		length, source, err := conn.ReadFromUDP(buffer)
		if err != nil {
			return found
		}
		if len(found) < maxCandidates {
			found = append(found, parseDiscovery(buffer[:length], source.IP)...)
		}
	}
}

func discoveryQuery() ([]byte, error) {
	name, err := dnsmessage.NewName(discoveryService)
	if err != nil {
		return nil, err
	}
	message := dnsmessage.Message{Questions: []dnsmessage.Question{{
		Name: name, Type: dnsmessage.TypePTR, Class: dnsmessage.ClassINET | askUnicast,
	}}}
	return message.Pack()
}

// parseDiscovery reads an answer, which may hold other services and other hosts too, in any order. So
// each garland is followed from its service to its host to that host's address. With no address of
// its own, it is where the answer came from.
func parseDiscovery(data []byte, source net.IP) []Found {
	var parser dnsmessage.Parser
	if _, err := parser.Start(data); err != nil {
		return nil
	}
	if parser.SkipAllQuestions() != nil {
		return nil
	}
	var resources []dnsmessage.Resource
	for _, read := range []func() ([]dnsmessage.Resource, error){parser.AllAnswers, parser.AllAuthorities, parser.AllAdditionals} {
		part, err := read()
		if err != nil {
			return nil
		}
		resources = append(resources, part...)
	}
	var instances []string
	services := map[string]*dnsmessage.SRVResource{}
	hosts := map[string]net.IP{}
	note := func(instance string) {
		if !slices.Contains(instances, instance) {
			instances = append(instances, instance)
		}
	}
	for _, resource := range resources {
		name := strings.ToLower(resource.Header.Name.String())
		switch body := resource.Body.(type) {
		case *dnsmessage.PTRResource:
			if name == discoveryService {
				note(strings.ToLower(body.PTR.String()))
			}
		case *dnsmessage.SRVResource:
			if strings.HasSuffix(name, "."+discoveryService) {
				note(name)
				services[name] = body
			}
		case *dnsmessage.AResource:
			if _, seen := hosts[name]; !seen {
				hosts[name] = net.IP(body.A[:])
			}
		}
	}
	var found []Found
	for _, instance := range instances {
		ip := source
		port := uint16(defaultPort)
		name := ""
		if service, known := services[instance]; known {
			port = service.Port
			target := service.Target.String()
			name = strings.TrimSuffix(target, ".")
			if address, known := hosts[strings.ToLower(target)]; known {
				ip = address
			}
		}
		if ip == nil {
			continue
		}
		address := ip.String()
		if port != defaultPort {
			address = net.JoinHostPort(address, strconv.Itoa(int(port)))
		}
		if name == "" {
			name = address
		}
		found = append(found, Found{Name: name, Address: address})
	}
	return found
}

// candidates are the addresses to ask, each once, under the best name heard for it.
func candidates(heard []Found) []Found {
	var kept []Found
	for _, found := range heard {
		at := slices.IndexFunc(kept, func(other Found) bool { return other.Address == found.Address })
		switch {
		case at >= 0:
			if kept[at].Name == kept[at].Address {
				kept[at].Name = found.Name
			}
		case len(kept) < maxCandidates:
			kept = append(kept, found)
		}
	}
	return kept
}

// confirmed drops whatever does not say that it is a garland.
func confirmed(ctx context.Context, heard []Found) []Found {
	asking, cancel := context.WithTimeout(ctx, confirmWait)
	defer cancel()
	var kept []Found
	var mutex sync.Mutex
	var waiting sync.WaitGroup
	turns := make(chan struct{}, maxConfirming)
	for _, found := range candidates(heard) {
		select {
		case turns <- struct{}{}:
		case <-asking.Done():
			waiting.Wait()
			return sorted(kept)
		}
		waiting.Add(1)
		go func() {
			defer waiting.Done()
			defer func() { <-turns }()
			about, fingerprint, err := probe(asking, found.Address)
			if err != nil || about.Product != productName || about.API < 1 {
				return
			}
			found.Version = about.Version
			found.Fingerprint = fingerprint
			mutex.Lock()
			kept = append(kept, found)
			mutex.Unlock()
		}()
	}
	waiting.Wait()
	return sorted(kept)
}

func sorted(found []Found) []Found {
	slices.SortFunc(found, func(a, b Found) int { return strings.Compare(a.Name, b.Name) })
	return found
}

// probe asks with no trust at all, as FetchRoot does, as nothing can be checked yet, and over one
// connection, as a garland has few and each costs it a handshake. A root it will not give is no
// reason to hide it: the fingerprint is then left empty.
func probe(ctx context.Context, address string) (About, string, error) {
	base, err := BaseURL(address)
	if err != nil {
		return About{}, "", err
	}
	config := &tls.Config{InsecureSkipVerify: true, MinVersion: tls.VersionTLS12} //nolint:gosec // It says what it is, no more.
	transport := &http.Transport{
		TLSClientConfig:     config,
		DialContext:         (&net.Dialer{Timeout: confirmConnect}).DialContext,
		TLSHandshakeTimeout: confirmConnect,
		MaxConnsPerHost:     maxConnections,
	}
	client := &http.Client{Transport: transport}
	defer client.CloseIdleConnections()
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, base+prefix+"about", nil)
	if err != nil {
		return About{}, "", fmt.Errorf("make the request: %w", err)
	}
	response, err := client.Do(request)
	if err != nil {
		return About{}, "", fmt.Errorf("reach it: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return About{}, "", errors.New("it answered as no garland does")
	}
	var about About
	if err := json.NewDecoder(io.LimitReader(response.Body, maxRootBytes)).Decode(&about); err != nil {
		return About{}, "", fmt.Errorf("read its answer: %w", err)
	}
	// To the end, for the connection to be used again.
	_, _ = io.Copy(io.Discard, io.LimitReader(response.Body, maxRootBytes))
	root, err := readRoot(ctx, client, base)
	if err != nil {
		return about, "", nil //nolint:nilerr // Said above.
	}
	return about, root.Fingerprint, nil
}
