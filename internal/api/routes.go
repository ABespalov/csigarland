// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The garland's public routes, and the shapes of what they answer.
// Contributors: None

package api

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"time"
)

// About says what a client is talking to. It needs no pairing.
type About struct {
	Product    string `json:"product"`
	API        int    `json:"api"`
	Version    string `json:"version"`
	OnTrial    bool   `json:"on_trial"`
	TrialLeft  int    `json:"trial_left"` // Whole seconds of its trial left, 0 when it is out of time.
	Effects    int    `json:"effects"`
	PackDigest string `json:"pack_digest"`
	Image      string `json:"image"`
	Boot       string `json:"boot"`
}

// About asks what the garland is. A firmware from before the public API answers 404.
func (c *Client) About(ctx context.Context) (About, error) {
	var about About
	return about, c.get(ctx, "about", nil, &about)
}

// PairRequest is a garland's answer to asking for access.
type PairRequest struct {
	Request string `json:"request"`
	Code    string `json:"code"`
	Seconds int    `json:"seconds"`
}

// Pair states.
const (
	PairWaiting  = "waiting"
	PairApproved = "approved"
	PairGone     = "gone"
)

// PairAnswer says how a request stands, with the token once approved.
type PairAnswer struct {
	State string `json:"state"`
	Token string `json:"token"`
}

// RequestPairing asks for access. The code is for the owner to compare with the garland's screen.
func (c *Client) RequestPairing(ctx context.Context, label string) (PairRequest, error) {
	var answer PairRequest
	return answer, c.post(ctx, "pair/request", map[string]string{"label": label}, &answer)
}

// WaitPairing asks how a request stands. The token comes once only.
func (c *Client) WaitPairing(ctx context.Context, request string) (PairAnswer, error) {
	var answer PairAnswer
	return answer, c.post(ctx, "pair/wait", map[string]string{"request": request}, &answer)
}

// Leds is the LED chain as the garland sees it.
type Leds struct {
	Setting  int    `json:"setting"`
	Count    int    `json:"count"`
	Kept     int    `json:"kept"`
	Max      int    `json:"max"`
	Probe    string `json:"probe"`
	Estimate int    `json:"estimate"`
}

// State is what the garland is doing.
type State struct {
	Power      bool    `json:"power"`
	Brightness int     `json:"brightness"`
	Effect     string  `json:"effect"`
	EffectName string  `json:"effect_name"`
	Speed      float64 `json:"speed"`
	Leds       Leds    `json:"leds"`
}

// State reads what the garland is doing.
func (c *Client) State(ctx context.Context) (State, error) {
	var state State
	return state, c.get(ctx, "state", nil, &state)
}

// SetBrightness sets the brightness in percent, which turns nothing on.
func (c *Client) SetBrightness(ctx context.Context, percent int) (State, error) {
	var state State
	return state, c.post(ctx, "state", map[string]int{"brightness": percent}, &state)
}

// SetSpeed sets the speed, 0.25 to 5 in steps of 0.25, which turns nothing on.
func (c *Client) SetSpeed(ctx context.Context, speed float64) (State, error) {
	var state State
	return state, c.post(ctx, "state", map[string]float64{"speed": speed}, &state)
}

// SetPower turns the garland on or off.
func (c *Client) SetPower(ctx context.Context, on bool) (State, error) {
	var state State
	return state, c.post(ctx, "power", map[string]bool{"on": on}, &state)
}

// SetLeds sets the LED count, 0 to detect it. It answers with the state.
func (c *Client) SetLeds(ctx context.Context, count int) (State, error) {
	var state State
	return state, c.post(ctx, "leds", map[string]int{"count": count}, &state)
}

// Effect is one effect of the catalog.
type Effect struct {
	ID       string `json:"id"`
	Name     string `json:"name"`
	Group    string `json:"group"`
	Favorite bool   `json:"favorite"`
	Active   bool   `json:"active"`
	Uploaded bool   `json:"uploaded"`
	BuiltIn  bool   `json:"built_in"`
}

// Effects lists every effect.
func (c *Client) Effects(ctx context.Context) ([]Effect, error) {
	var answer struct {
		Effects []Effect `json:"effects"`
	}
	return answer.Effects, c.get(ctx, "effects", nil, &answer)
}

// Play starts an effect, which also turns the garland on.
func (c *Client) Play(ctx context.Context, id string) error {
	return c.post(ctx, "effects/play", map[string]string{"id": id}, nil)
}

// SetFavorite ticks or unticks an effect for the EFFECT button.
func (c *Client) SetFavorite(ctx context.Context, id string, favorite bool) error {
	return c.post(ctx, "effects/favorite", map[string]any{"id": id, "favorite": favorite}, nil)
}

// EffectFile reads the file of any effect, as it stands on this garland.
func (c *Client) EffectFile(ctx context.Context, id string) ([]byte, error) {
	response, err := c.request(ctx, http.MethodGet, "effects/file", url.Values{"id": {id}}, nil, "")
	if err != nil {
		return nil, err
	}
	defer response.Body.Close()
	data, err := io.ReadAll(io.LimitReader(response.Body, maxAnswerBytes))
	if err != nil {
		return nil, fmt.Errorf("read the effect: %w", err)
	}
	return data, nil
}

// SaveEffect keeps a file under an id. An id of a built-in effect replaces that effect.
func (c *Client) SaveEffect(ctx context.Context, id string, file []byte) error {
	response, err := c.request(ctx, http.MethodPost, "effects/file", url.Values{"id": {id}}, bytes.NewReader(file), "application/json")
	if err != nil {
		return err
	}
	return decode(response, nil)
}

// DeleteEffect removes an uploaded effect, or gives a built-in one its own definition back.
func (c *Client) DeleteEffect(ctx context.Context, id string) error {
	response, err := c.request(ctx, http.MethodDelete, "effects/file", url.Values{"id": {id}}, nil, "")
	if err != nil {
		return err
	}
	return decode(response, nil)
}

// PreviewEffect plays a file on the garland without saving it, until another effect is chosen.
func (c *Client) PreviewEffect(ctx context.Context, file []byte) error {
	response, err := c.request(ctx, http.MethodPost, "effects/preview", nil, bytes.NewReader(file), "application/json")
	if err != nil {
		return err
	}
	return decode(response, nil)
}

// EffectStore is the room for uploaded effects.
type EffectStore struct {
	Available bool `json:"available"`
	Uploaded  int  `json:"uploaded"`
	Max       int  `json:"max"`
}

// EffectStore reads how many effects are uploaded, and how many can be.
func (c *Client) EffectStore(ctx context.Context) (EffectStore, error) {
	var store EffectStore
	return store, c.get(ctx, "effects/store", nil, &store)
}

// Power steps of a scheduled action.
const (
	StepOn  = "on"
	StepOff = "off"
)

// Action is one scheduled action.
type Action struct {
	Enabled    bool    `json:"enabled"`
	Time       string  `json:"time"`
	Days       int     `json:"days"` // A bit for each day, 0 being Sunday.
	Power      string  `json:"power,omitempty"`
	Brightness int     `json:"brightness,omitempty"`
	Speed      float64 `json:"speed,omitempty"`
	Effect     string  `json:"effect,omitempty"`
}

// NextAction is what the schedule does next.
type NextAction struct {
	Clock      bool    `json:"clock"`
	At         string  `json:"at"`
	Power      string  `json:"power"`
	Brightness int     `json:"brightness"`
	Speed      float64 `json:"speed"`
	EffectName string  `json:"effect_name"`
}

// Schedule is the whole list of actions.
type Schedule struct {
	Entries  []Action   `json:"entries"`
	Max      int        `json:"max"`
	Revision uint32     `json:"revision"`
	Next     NextAction `json:"next"`
}

// Schedule reads the list of actions.
func (c *Client) Schedule(ctx context.Context) (Schedule, error) {
	var schedule Schedule
	return schedule, c.get(ctx, "schedule", nil, &schedule)
}

// SetSchedule replaces the whole list. If it changed since revision, this is a conflict: see
// IsConflict.
func (c *Client) SetSchedule(ctx context.Context, entries []Action, revision uint32) (Schedule, error) {
	var schedule Schedule
	// A list, even an empty one: the garland refuses null.
	if entries == nil {
		entries = []Action{}
	}
	body := map[string]any{"entries": entries, "revision": revision}
	return schedule, c.post(ctx, "schedule", body, &schedule)
}

// ClockModule is the optional DS3231.
type ClockModule struct {
	Present bool   `json:"present"`
	Trusted bool   `json:"trusted"`
	Now     string `json:"now"`
	Source  string `json:"source"`
}

// Clock is the garland's time.
type Clock struct {
	Synced  bool        `json:"synced"`
	Now     string      `json:"now"`
	Zone    string      `json:"zone"`
	ZoneSet bool        `json:"zone_set"`
	Module  ClockModule `json:"rtc"`
}

// Clock reads the garland's time.
func (c *Client) Clock(ctx context.Context) (Clock, error) {
	var clock Clock
	return clock, c.get(ctx, "time", nil, &clock)
}

// SetZone sets the time zone, as a POSIX TZ string.
func (c *Client) SetZone(ctx context.Context, zone string) (Clock, error) {
	var clock Clock
	return clock, c.post(ctx, "time", map[string]string{"zone": zone}, &clock)
}

// SetModuleFromInternet has the clock module set from NTP.
func (c *Client) SetModuleFromInternet(ctx context.Context) (Clock, error) {
	var clock Clock
	return clock, c.post(ctx, "time/rtc", map[string]string{"source": "ntp"}, &clock)
}

// SetModuleFromClock sets the clock module from this computer's.
func (c *Client) SetModuleFromClock(ctx context.Context, now time.Time) (Clock, error) {
	var clock Clock
	return clock, c.post(ctx, "time/rtc", map[string]any{"source": "browser", "epoch": now.Unix()}, &clock)
}

// SetModuleByHand sets the clock module to a time in the garland's zone: 2026-10-31T18:05.
func (c *Client) SetModuleByHand(ctx context.Context, local string) (Clock, error) {
	var clock Clock
	return clock, c.post(ctx, "time/rtc", map[string]string{"source": "manual", "local": local}, &clock)
}

// Display is the screen's settings.
type Display struct {
	Saver            string `json:"saver"`
	Delay            int    `json:"delay"`
	DelayMin         int    `json:"delay_min,omitempty"`
	DelayMax         int    `json:"delay_max,omitempty"`
	ScreenBrightness int    `json:"screen_brightness"`
	SaverBrightness  int    `json:"saver_brightness"`
	BrightnessMin    int    `json:"brightness_min,omitempty"`
}

// Display reads the screen's settings.
func (c *Client) Display(ctx context.Context) (Display, error) {
	var display Display
	return display, c.get(ctx, "display", nil, &display)
}

// SetDisplay saves the screen's settings.
func (c *Client) SetDisplay(ctx context.Context, display Display) (Display, error) {
	var answer Display
	body := Display{Saver: display.Saver, Delay: display.Delay, ScreenBrightness: display.ScreenBrightness, SaverBrightness: display.SaverBrightness}
	return answer, c.post(ctx, "display", body, &answer)
}

// Wifi is the state of the home network.
type Wifi struct {
	Configured   bool   `json:"configured"`
	Connected    bool   `json:"connected"`
	SetupNetwork bool   `json:"setup_network"`
	SSID         string `json:"ssid"`
	IP           string `json:"ip"`
	Hostname     string `json:"hostname"`
}

// Wifi reads the state of the home network.
func (c *Client) Wifi(ctx context.Context) (Wifi, error) {
	var wifi Wifi
	return wifi, c.get(ctx, "wifi", nil, &wifi)
}

// JoinWifi joins a network, or with an empty ssid forgets the one there is.
func (c *Client) JoinWifi(ctx context.Context, ssid, password string) (Wifi, error) {
	var wifi Wifi
	return wifi, c.post(ctx, "wifi", map[string]string{"ssid": ssid, "password": password}, &wifi)
}

// Network is one that a search found.
type Network struct {
	SSID string `json:"ssid"`
	RSSI int    `json:"rssi"`
	Open bool   `json:"open"`
}

// Search states.
const (
	ScanRunning = "running"
	ScanFailed  = "failed"
)

// Scan is how a search for networks stands.
type Scan struct {
	State    string    `json:"state"`
	Networks []Network `json:"networks"`
}

// StartScan starts a search in the background.
func (c *Client) StartScan(ctx context.Context) (Scan, error) {
	var scan Scan
	return scan, c.post(ctx, "wifi/scan", map[string]string{}, &scan)
}

// Scan reads how a search stands.
func (c *Client) Scan(ctx context.Context) (Scan, error) {
	var scan Scan
	return scan, c.get(ctx, "wifi/scan", nil, &scan)
}

// SetName sets the mDNS name, without .local. The garland restarts if it changes.
func (c *Client) SetName(ctx context.Context, name string) error {
	return c.post(ctx, "network/name", map[string]string{"name": name}, nil)
}

// SetDomain sets the owner's one domain or fixed address, empty for none. The garland restarts.
func (c *Client) SetDomain(ctx context.Context, domain string) error {
	return c.post(ctx, "network/certificate", map[string]string{"domain": domain}, nil)
}

// Firmware is what is running, as the update routes tell it.
type Firmware struct {
	Version    string `json:"version"`
	OnTrial    bool   `json:"on_trial"`
	TrialLeft  int    `json:"trial_left"` // Whole seconds of its trial left, 0 when it is out of time.
	Effects    int    `json:"effects"`
	PackDigest string `json:"pack_digest"`
	Image      string `json:"image"`
	Boot       string `json:"boot"`
}

// Firmware reads what is running.
func (c *Client) Firmware(ctx context.Context) (Firmware, error) {
	var firmware Firmware
	return firmware, c.get(ctx, "firmware", nil, &firmware)
}

// ErrInstallStalled says an upload stopped moving.
var ErrInstallStalled = errors.New("the garland stopped taking the update")

// A reader that says each time it is read from, and how far it has got.
type movingReader struct {
	from  io.Reader
	moved func()
	read  int64
	total int64
	told  func(sent, total int64)
}

func (r *movingReader) Read(into []byte) (int, error) {
	r.moved()
	count, err := r.from.Read(into)
	r.read += int64(count)
	if r.told != nil && count > 0 {
		r.told(r.read, r.total)
	}
	return count, err
}

// Install sends an update file, cancelled by ctx. An upload takes minutes, so it has no limit as a
// whole, only while it keeps moving. The garland checks its signature, installs what it holds and
// restarts: see JudgeUpdate for what to watch. progress, which may be nil, hears how much has gone to
// the connection, away from the caller's goroutine. It is not to wait for anything.
func (c *Client) Install(ctx context.Context, file []byte, progress func(sent, total int64)) error {
	ctx, cancel := context.WithCancelCause(ctx)
	defer cancel(nil)
	watchdog := time.AfterFunc(c.stall, func() { cancel(ErrInstallStalled) })
	defer watchdog.Stop()
	address := c.base + prefix + "firmware"
	body := func() io.ReadCloser {
		return io.NopCloser(&movingReader{
			from: bytes.NewReader(file), moved: func() { watchdog.Reset(c.stall) },
			total: int64(len(file)), told: progress,
		})
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodPost, address, body())
	if err != nil {
		return fmt.Errorf("make the request: %w", err)
	}
	request.ContentLength = int64(len(file))
	// Go uses this only for a socket found closed before a byte went, which is always safe.
	request.GetBody = func() (io.ReadCloser, error) { return body(), nil }
	request.Header.Set("Content-Type", "application/octet-stream")
	if c.token != "" {
		request.Header.Set("Authorization", bearerScheme+c.token)
	}
	long := &http.Client{Transport: c.http.Transport}
	response, err := long.Do(request)
	if err != nil {
		if cause := context.Cause(ctx); errors.Is(cause, ErrInstallStalled) {
			return fmt.Errorf("send the update: %w", cause)
		}
		return fmt.Errorf("send the update: %w", err)
	}
	defer response.Body.Close()
	if response.StatusCode >= http.StatusBadRequest {
		return readError(response)
	}
	return nil
}

// Info is the board, its memory, addresses and certificate.
type Info struct {
	Model            string     `json:"model"`
	Chip             string     `json:"chip"`
	MAC              string     `json:"mac"`
	RAMTotal         int64      `json:"ram_total"`
	RAMFree          int64      `json:"ram_free"`
	RAMLowest        int64      `json:"ram_lowest"`
	FlashTotal       int64      `json:"flash_total"`
	FirmwareSlot     int64      `json:"firmware_slot"`
	FirmwareUsed     int64      `json:"firmware_used"`
	IP               string     `json:"ip"`
	SetupNetwork     bool       `json:"setup_network"`
	Hostname         string     `json:"hostname"`
	HTTPS            bool       `json:"https"`
	LedOutput        bool       `json:"led_output"`
	CertificateNames string     `json:"certificate_names"`
	Domain           string     `json:"certificate_domain"`
	RootFingerprint  string     `json:"root_fingerprint"`
	CertificateUntil int64      `json:"certificate_until"`
	Effects          int        `json:"effects"`
	Favorites        int        `json:"favorites"`
	Power            bool       `json:"power"`
	EffectName       string     `json:"effect_name"`
	Brightness       int        `json:"brightness"`
	Speed            float64    `json:"speed"`
	Next             NextAction `json:"next"`
}

// Info reads the board's particulars.
func (c *Client) Info(ctx context.Context) (Info, error) {
	var info Info
	return info, c.get(ctx, "info", nil, &info)
}

// PairedClient is a browser or program with access.
type PairedClient struct {
	ID      int    `json:"id"`
	Label   string `json:"label"`
	Paired  int64  `json:"paired"`
	Expires int64  `json:"expires"`
	This    bool   `json:"this"`
}

type clientList struct {
	Devices []PairedClient `json:"devices"`
}

// Clients lists what has access.
func (c *Client) Clients(ctx context.Context) ([]PairedClient, error) {
	var answer clientList
	return answer.Devices, c.get(ctx, "clients", nil, &answer)
}

// ForgetClient takes access away, and answers with who is left.
func (c *Client) ForgetClient(ctx context.Context, id int) ([]PairedClient, error) {
	var answer clientList
	return answer.Devices, c.post(ctx, "clients/forget", map[string]int{"id": id}, &answer)
}

// LogPart is what a garland logged since a position.
type LogPart struct {
	Text     string
	Position uint32 // To ask from next.
	Lost     uint32 // Bytes that went before they were read.
	Boot     string // Which start of the garland: a position means nothing in another.
	Missed   uint32 // Lines that found the log being sent, since the last look.
}

// Log reads the log since a position, 0 at first. The first look starts the garland keeping it, so
// what came before then is not there.
func (c *Client) Log(ctx context.Context, after uint32) (LogPart, error) {
	query := url.Values{"after": {strconv.FormatUint(uint64(after), 10)}}
	response, err := c.request(ctx, http.MethodGet, "log", query, nil, "")
	if err != nil {
		return LogPart{}, err
	}
	defer response.Body.Close()
	data, err := io.ReadAll(io.LimitReader(response.Body, maxAnswerBytes))
	if err != nil {
		return LogPart{}, fmt.Errorf("read the log: %w", err)
	}
	position, err := strconv.ParseUint(response.Header.Get("X-Log-Position"), 10, 32)
	if err != nil {
		return LogPart{}, fmt.Errorf("read the log's position: %w", err)
	}
	lost, err := strconv.ParseUint(response.Header.Get("X-Log-Lost"), 10, 32)
	if err != nil {
		return LogPart{}, fmt.Errorf("read what the log lost: %w", err)
	}
	part := LogPart{Text: string(data), Position: uint32(position), Lost: uint32(lost), Boot: response.Header.Get("X-Log-Boot")}
	if missed, err := strconv.ParseUint(response.Header.Get("X-Log-Missed"), 10, 32); err == nil {
		part.Missed = uint32(missed)
	}
	return part, nil
}
