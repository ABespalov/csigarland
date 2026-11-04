// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: A client for a garland's public API: its address, pinned root, token and errors.
// Contributors: None

// Package api talks to a garland over /api/v1/, as docs/stubs/PUBLIC-API.md describes.
//
// A garland's certificate comes from its own private root, so trust is pinned rather than taken
// from the system: see FetchRoot and New.
package api

import (
	"bytes"
	"context"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
)

const (
	// Version is the version of the API this client speaks.
	Version = 1

	prefix         = "/api/v1/"
	rootPath       = "/csigarland-root.crt"
	requestTimeout = 15 * time.Second
	maxAnswerBytes = 1 << 20
	maxRootBytes   = 16 << 10
	bearerScheme   = "Bearer "
	// A garland serves few sockets, and each TLS session costs it memory.
	maxConnections = 1
	// No byte taken by the socket for this long, or no answer after the last, means it has hung.
	installStall   = 60 * time.Second
	idempotencyKey = "Idempotency-Key"
)

// ErrWrongRoot says the garland did not present a chain to the root it was paired under.
var ErrWrongRoot = errors.New("the garland's certificate does not come from the root kept for it")

// ErrCertificateDates says the garland's chain is its own, but not valid at this computer's time: a
// clock is wrong here or on the garland, which renews its certificate once it knows the time. Pairing
// again would not help.
var ErrCertificateDates = errors.New("the garland's certificate is not valid at this computer's time: check the clock here, and the garland's")

// Error is what a garland answers a failed request with.
type Error struct {
	Status     int
	Code       string
	Message    string
	RetryAfter time.Duration
}

func (e *Error) Error() string {
	if e.Message != "" {
		return e.Message
	}
	return fmt.Sprintf("the garland answered %d", e.Status)
}

// IsNotPaired reports whether err says the client has to pair again.
func IsNotPaired(err error) bool {
	var failure *Error
	return errors.As(err, &failure) && failure.Status == http.StatusUnauthorized
}

// IsConflict reports whether err says what was sent was based on something since changed.
func IsConflict(err error) bool {
	var failure *Error
	return errors.As(err, &failure) && failure.Status == http.StatusConflict
}

// CleanAddress is the one spelling of an address, however the owner typed it: a host in lowercase,
// with a port only if it is not the usual one. Two that mean the same come out the same.
func CleanAddress(address string) (string, error) {
	cleaned := strings.TrimSpace(address)
	for _, scheme := range []string{"https://", "http://"} {
		if len(cleaned) >= len(scheme) && strings.EqualFold(cleaned[:len(scheme)], scheme) {
			cleaned = cleaned[len(scheme):]
			break
		}
	}
	cleaned = strings.TrimRight(cleaned, "/")
	if cleaned == "" || strings.ContainsAny(cleaned, "/?#@ ") {
		return "", fmt.Errorf("%q is not an address", address)
	}
	parsed, err := url.Parse("https://" + cleaned)
	if err != nil || parsed.Hostname() == "" {
		return "", fmt.Errorf("%q is not an address", address)
	}
	host := strings.ToLower(parsed.Hostname())
	port := parsed.Port()
	if number, err := strconv.Atoi(port); port != "" && (err != nil || number < 1 || number > 65535) {
		return "", fmt.Errorf("%q has no such port", address)
	}
	if port != "" && port != strconv.Itoa(defaultPort) {
		return net.JoinHostPort(host, port), nil
	}
	if strings.Contains(host, ":") {
		return "[" + host + "]", nil
	}
	return host, nil
}

// BaseURL is the address the owner gave, as a URL with no path: a host, or a host and port.
func BaseURL(address string) (string, error) {
	cleaned, err := CleanAddress(address)
	if err != nil {
		return "", err
	}
	return "https://" + cleaned, nil
}

// Root is a garland's root certificate, as first seen.
type Root struct {
	PEM         []byte
	Fingerprint string // SHA-256 of the certificate, in hexadecimal, as the portal shows it.
}

// ParseRoot reads a root certificate and works out its fingerprint.
func ParseRoot(data []byte) (Root, error) {
	block, _ := pem.Decode(data)
	if block == nil || block.Type != "CERTIFICATE" {
		return Root{}, errors.New("that is not a certificate")
	}
	certificate, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return Root{}, fmt.Errorf("read the certificate: %w", err)
	}
	sum := sha256.Sum256(certificate.Raw)
	// Only what the fingerprint is of. Whatever came after it in the file was never compared with
	// anything, so it must not be trusted along with it.
	only := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: certificate.Raw})
	return Root{PEM: only, Fingerprint: strings.ToUpper(hex.EncodeToString(sum[:]))}, nil
}

// FetchRoot asks a garland for its root with no trust at all, as nothing can be checked yet. Its
// fingerprint is for the owner to compare with the one on the garland's Info page before trusting
// it, which is the whole of what makes this first contact safe.
func FetchRoot(ctx context.Context, address string) (Root, error) {
	base, err := BaseURL(address)
	if err != nil {
		return Root{}, err
	}
	config := &tls.Config{InsecureSkipVerify: true, MinVersion: tls.VersionTLS12} //nolint:gosec // The owner checks the fingerprint.
	client := &http.Client{Timeout: requestTimeout, Transport: &http.Transport{TLSClientConfig: config}}
	defer client.CloseIdleConnections()
	return readRoot(ctx, client, base)
}

func readRoot(ctx context.Context, client *http.Client, base string) (Root, error) {
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, base+rootPath, nil)
	if err != nil {
		return Root{}, fmt.Errorf("ask for the root: %w", err)
	}
	response, err := client.Do(request)
	if err != nil {
		return Root{}, fmt.Errorf("reach the garland: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return Root{}, fmt.Errorf("the garland answered %d for its root", response.StatusCode)
	}
	data, err := io.ReadAll(io.LimitReader(response.Body, maxRootBytes))
	if err != nil {
		return Root{}, fmt.Errorf("read the root: %w", err)
	}
	return ParseRoot(data)
}

// Client speaks to one garland. It is safe for use by several goroutines.
type Client struct {
	base  string
	http  *http.Client
	token string
	stall time.Duration
}

// New returns a client for a garland at address that trusts only rootPEM. The token may be empty
// until the client has paired.
func New(address string, rootPEM []byte, token string) (*Client, error) {
	base, err := BaseURL(address)
	if err != nil {
		return nil, err
	}
	root, err := ParseRoot(rootPEM)
	if err != nil {
		return nil, fmt.Errorf("the root kept for this garland cannot be read: %w", err)
	}
	roots := x509.NewCertPool()
	if !roots.AppendCertsFromPEM(root.PEM) {
		return nil, errors.New("the root kept for this garland cannot be read")
	}
	transport := &http.Transport{
		MaxConnsPerHost:     maxConnections,
		MaxIdleConnsPerHost: maxConnections,
		IdleConnTimeout:     idleConnection,
		DialContext:         (&net.Dialer{Timeout: 5 * time.Second}).DialContext,
		TLSHandshakeTimeout: requestTimeout,
		TLSClientConfig: &tls.Config{
			MinVersion: tls.VersionTLS12,
			// The address is whatever the owner typed, which need not be among the certificate's
			// names. A chain to this garland's own root is what identifies it.
			InsecureSkipVerify: true, //nolint:gosec // VerifyConnection below does the checking.
			VerifyConnection:   func(state tls.ConnectionState) error { return verifyChain(state, roots) },
		},
	}
	return &Client{base: base, token: token, stall: installStall, http: &http.Client{Transport: transport, Timeout: requestTimeout}}, nil
}

// idleConnection is how long a connection waits for its next request here: less than the garland's own
// limit on one, kHeaderDeadlineUs in firmware/app/src/portal_watch.h, past which it closes it. A request
// sent on one it is closing would fail, and one that changes something is not sent twice by itself.
const idleConnection = 8 * time.Second

func verifyChain(state tls.ConnectionState, roots *x509.CertPool) error {
	if len(state.PeerCertificates) == 0 {
		return ErrWrongRoot
	}
	intermediates := x509.NewCertPool()
	for _, certificate := range state.PeerCertificates[1:] {
		intermediates.AddCert(certificate)
	}
	leaf := state.PeerCertificates[0]
	options := x509.VerifyOptions{Roots: roots, Intermediates: intermediates, KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}}
	_, err := leaf.Verify(options)
	if err == nil {
		return nil
	}
	// Dates are checked ahead of the chain, so only a chain that holds at a time the certificate
	// itself allows is the garland's own with a clock wrong somewhere.
	var invalid x509.CertificateInvalidError
	if errors.As(err, &invalid) && invalid.Reason == x509.Expired {
		options.CurrentTime = leaf.NotBefore
		if _, chainErr := leaf.Verify(options); chainErr == nil {
			return fmt.Errorf("%w: %w", ErrCertificateDates, err)
		}
	}
	return fmt.Errorf("%w: %w", ErrWrongRoot, err)
}

// WithToken returns a client that sends token, sharing this one's connections.
func (c *Client) WithToken(token string) *Client {
	return &Client{base: c.base, http: c.http, token: token, stall: c.stall}
}

// Close ends the client's idle connection.
func (c *Client) Close() {
	c.http.CloseIdleConnections()
}

func (c *Client) request(ctx context.Context, method, route string, query url.Values, body io.Reader, kind string) (*http.Response, error) {
	address := c.base + prefix + route
	if len(query) > 0 {
		address += "?" + query.Encode()
	}
	request, err := http.NewRequestWithContext(ctx, method, address, body)
	if err != nil {
		return nil, fmt.Errorf("make the request: %w", err)
	}
	if kind != "" {
		request.Header.Set("Content-Type", kind)
	}
	if c.token != "" {
		request.Header.Set("Authorization", bearerScheme+c.token)
	}
	if safeToSendTwice(method, route) {
		request.Header.Set(idempotencyKey, "1")
	}
	response, err := c.http.Do(request)
	if err != nil {
		return nil, fmt.Errorf("reach the garland: %w", err)
	}
	if response.StatusCode >= http.StatusBadRequest {
		defer response.Body.Close()
		return nil, readError(response)
	}
	return response, nil
}

// The garland closes an idle socket to serve someone else, and this has Go send again on a new one
// rather than fail. A socket can also close after the garland acted, so it is only for what sets a
// value, costs nothing done twice, and would say the same again. So not pairing, the schedule, a
// delete, what writes flash, forgetting a client, or what has the garland leave the network.
func safeToSendTwice(method, route string) bool {
	if method != http.MethodPost {
		return false
	}
	switch route {
	case "state", "power", "leds", "display", "time", "effects/play", "effects/favorite", "effects/preview":
		return true
	}
	return false
}

func readError(response *http.Response) error {
	failure := &Error{Status: response.StatusCode}
	if seconds, err := strconv.Atoi(response.Header.Get("Retry-After")); err == nil {
		failure.RetryAfter = time.Duration(seconds) * time.Second
	}
	data, err := io.ReadAll(io.LimitReader(response.Body, maxAnswerBytes))
	if err != nil {
		return failure
	}
	var answer struct {
		Error struct {
			Code    string `json:"code"`
			Message string `json:"message"`
		} `json:"error"`
	}
	if json.Unmarshal(data, &answer) == nil && answer.Error.Message != "" {
		failure.Code = answer.Error.Code
		failure.Message = answer.Error.Message
	} else {
		failure.Message = strings.TrimSpace(string(data))
	}
	return failure
}

func decode(response *http.Response, into any) error {
	defer response.Body.Close()
	data, err := io.ReadAll(io.LimitReader(response.Body, maxAnswerBytes))
	if err != nil {
		return fmt.Errorf("read the answer: %w", err)
	}
	if into == nil {
		return nil
	}
	if err := json.Unmarshal(data, into); err != nil {
		return fmt.Errorf("read the answer: %w", err)
	}
	return nil
}

func (c *Client) get(ctx context.Context, route string, query url.Values, into any) error {
	response, err := c.request(ctx, http.MethodGet, route, query, nil, "")
	if err != nil {
		return err
	}
	return decode(response, into)
}

// As short as JSON allows: the garland bounds a request's bytes, and json.Marshal would make six of
// each &, < and > in a Wi-Fi password.
func marshal(body any) ([]byte, error) {
	var out bytes.Buffer
	encoder := json.NewEncoder(&out)
	encoder.SetEscapeHTML(false)
	if err := encoder.Encode(body); err != nil {
		return nil, err
	}
	return bytes.TrimSpace(out.Bytes()), nil
}

func (c *Client) post(ctx context.Context, route string, body, into any) error {
	data, err := marshal(body)
	if err != nil {
		return fmt.Errorf("write the request: %w", err)
	}
	response, err := c.request(ctx, http.MethodPost, route, nil, bytes.NewReader(data), "application/json")
	if err != nil {
		return err
	}
	return decode(response, into)
}
