// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: What the window works out, apart from drawing it, so that it can be tested.
// Contributors: None

package ui

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"strings"

	"github.com/ABespalov/csigarland/internal/api"
	"github.com/ABespalov/csigarland/internal/settings"
)

// GarlandRow is one line of the list to connect from.
type GarlandRow struct {
	Name        string
	Address     string
	Version     string
	Fingerprint string
	Known       bool // Connected to before.
	Paired      bool // With a token kept for it.
	Answering   bool // On the network now.
}

// GarlandRows lists those met before, the last used first, whether they answered or not, and then the
// others that answered. A root says which is which, as an address may change.
func GarlandRows(known []settings.Garland, paired func(fingerprint string) bool, found []api.Found) []GarlandRow {
	rows := make([]GarlandRow, 0, len(known)+len(found))
	for _, garland := range known {
		rows = append(rows, GarlandRow{Name: garland.Name, Address: garland.Address, Fingerprint: garland.Fingerprint,
			Known: true, Paired: paired(garland.Fingerprint)})
	}
next:
	for _, answered := range found {
		for i := range rows {
			// By its root, or by its address if it would not give that this time.
			same := rows[i].Fingerprint == answered.Fingerprint
			if answered.Fingerprint == "" {
				same = rows[i].Address == answered.Address
			}
			if same && rows[i].Known {
				rows[i].Address, rows[i].Version, rows[i].Answering = answered.Address, answered.Version, true
				continue next
			}
		}
		rows = append(rows, GarlandRow{Name: answered.Name, Address: answered.Address, Version: answered.Version,
			Fingerprint: answered.Fingerprint, Answering: true})
	}
	return rows
}

// Text is the line under a row's name.
func (r GarlandRow) Text() string {
	parts := []string{r.Address}
	if r.Version != "" {
		parts = append(parts, "firmware "+r.Version)
	}
	if r.Paired {
		parts = append(parts, "paired")
	}
	if !r.Answering {
		parts = append(parts, "not answering")
	}
	return strings.Join(parts, ", ")
}

// UploadedGroup is the group the firmware names for what the owner uploaded. It comes last.
const UploadedGroup = "Uploaded"

const (
	quartersPerSpeed = 4
	speedMinQuarters = 1
	speedMaxQuarters = 20
	allDays          = 127
	minutesPerDay    = 24 * 60
	maxEffectIDBytes = 32
)

// EffectGroup is one group of the catalog, in order.
type EffectGroup struct {
	Name    string
	Effects []api.Effect
}

// GroupEffects keeps the catalog's order, of the groups and within them, with Uploaded last and
// always there.
func GroupEffects(effects []api.Effect) []EffectGroup {
	var groups []EffectGroup
	places := map[string]int{}
	var uploaded []api.Effect
	for _, effect := range effects {
		if effect.Group == UploadedGroup {
			uploaded = append(uploaded, effect)
			continue
		}
		place, known := places[effect.Group]
		if !known {
			place = len(groups)
			places[effect.Group] = place
			groups = append(groups, EffectGroup{Name: effect.Group})
		}
		groups[place].Effects = append(groups[place].Effects, effect)
	}
	return append(groups, EffectGroup{Name: UploadedGroup, Effects: uploaded})
}

// GroupTitle is as the portal has it: Classic Garland (8 of 16).
func GroupTitle(group EffectGroup) string {
	favorites := 0
	for _, effect := range group.Effects {
		if effect.Favorite {
			favorites++
		}
	}
	if favorites > 0 {
		return fmt.Sprintf("%s (%d of %d)", group.Name, favorites, len(group.Effects))
	}
	return fmt.Sprintf("%s (%d)", group.Name, len(group.Effects))
}

// SliderToSpeed turns a slider's place, 0 to 1, into a speed the garland takes.
func SliderToSpeed(place float32) float64 {
	quarters := speedMinQuarters + int(math.Round(float64(place)*(speedMaxQuarters-speedMinQuarters)))
	return float64(quarters) / quartersPerSpeed
}

// SpeedToSlider is the other way round.
func SpeedToSlider(speed float64) float32 {
	quarters := speed * quartersPerSpeed
	place := (quarters - speedMinQuarters) / (speedMaxQuarters - speedMinQuarters)
	return float32(math.Max(0, math.Min(1, place)))
}

// SliderToPercent turns a slider's place into a brightness.
func SliderToPercent(place float32) int {
	return 1 + int(math.Round(float64(place)*99))
}

// PercentToSlider is the other way round.
func PercentToSlider(percent int) float32 {
	return float32(math.Max(0, math.Min(1, float64(percent-1)/99)))
}

// SpeedText is as the garland's screen has it: 0.25x, 1x.
func SpeedText(speed float64) string {
	return strconv.FormatFloat(speed, 'f', -1, 64) + "x"
}

var dayNames = []string{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}

// DaysText says which days an action runs on.
func DaysText(days int) string {
	if days&allDays == allDays {
		return "every day"
	}
	var names []string
	for bit, name := range dayNames {
		if days&(1<<bit) != 0 {
			names = append(names, name)
		}
	}
	return strings.Join(names, ", ")
}

// StepsText says what an action does, as the portal does.
func StepsText(power string, brightness int, speed float64, effect string, playlist bool) string {
	if power == api.StepOff {
		return "Turn off"
	}
	var steps []string
	switch power {
	case api.StepOn:
		steps = append(steps, "Turn on")
	case api.StepRise:
		steps = append(steps, "Turn on, rising over a minute")
	}
	if brightness != 0 {
		steps = append(steps, fmt.Sprintf("brightness %d%%", brightness))
	}
	if speed != 0 {
		steps = append(steps, "speed "+SpeedText(speed))
	}
	if effect != "" {
		steps = append(steps, "effect "+effect)
	}
	if playlist {
		steps = append(steps, "start the playlist")
	}
	return strings.Join(steps, ", ")
}

// NextText says what the schedule does next.
func NextText(next api.NextAction) string {
	if next.Unread {
		return "The schedule could not be read yet: the garland tries again every minute."
	}
	if !next.Clock {
		return "Waiting for the clock to be set."
	}
	if next.At == "" {
		return "Nothing in the coming week."
	}
	return next.At + ": " + StepsText(next.Power, next.Brightness, next.Speed, next.EffectName, next.Playlist) + "."
}

var actionTime = regexp.MustCompile(`^([01]\d|2[0-3]):[0-5]\d$`)

// ActionForm is what the schedule's form holds.
type ActionForm struct {
	Enabled       bool
	Time          string
	Days          int
	TurnOff       bool
	TurnOn        bool
	Rise          bool // With TurnOn: rising from dark over a minute.
	UseBrightness bool
	Brightness    string
	UseSpeed      bool
	Speed         float64
	UseEffect     bool
	Effect        string
	UsePlaylist   bool // Start the playlist, in place of an effect.
}

// BuildAction checks the form as the portal's does, and the firmware again after it.
func BuildAction(form ActionForm) (api.Action, error) {
	if !actionTime.MatchString(form.Time) {
		return api.Action{}, errors.New("the time must be as 18:00")
	}
	if form.Days&allDays == 0 {
		return api.Action{}, errors.New("pick at least one day")
	}
	action := api.Action{Enabled: form.Enabled, Time: form.Time, Days: form.Days & allDays}
	if form.TurnOff {
		action.Power = api.StepOff
		return action, nil
	}
	if form.TurnOn {
		action.Power = api.StepOn
		if form.Rise {
			action.Power = api.StepRise
		}
	}
	if form.UseBrightness {
		percent, err := strconv.Atoi(strings.TrimSpace(form.Brightness))
		if err != nil || percent < 1 || percent > 100 {
			return api.Action{}, errors.New("brightness must be 1 to 100")
		}
		action.Brightness = percent
	}
	if form.UseSpeed {
		action.Speed = form.Speed
	}
	if form.UseEffect {
		if form.Effect == "" {
			return api.Action{}, errors.New("pick an effect")
		}
		action.Effect = form.Effect
	} else if form.UsePlaylist {
		action.Playlist = true
	}
	if action.Power == "" && action.Brightness == 0 && action.Speed == 0 && action.Effect == "" && !action.Playlist {
		return api.Action{}, errors.New("pick at least one step")
	}
	return action, nil
}

// ScheduleSpeeds are those the schedule takes, as the firmware's list has them.
// Arrays, so that the buttons for them are counted from them.
var ScheduleSpeeds = [...]float64{0.25, 0.5, 0.75, 1, 1.5, 2, 3}

// Zones are the portal's table of time zones, name and POSIX string.
var Zones = [...][2]string{
	{"UTC", "UTC0"},
	{"London, Dublin", "GMT0BST,M3.5.0/1,M10.5.0"},
	{"Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
	{"Berlin, Paris, Warsaw, Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
	{"Kyiv, Helsinki, Athens, Riga", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
	{"Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0"},
	{"Moscow, Minsk", "MSK-3"},
	{"Istanbul", "<+03>-3"},
	{"Dubai, Yerevan, Tbilisi, Baku", "<+04>-4"},
	{"Yekaterinburg, Tashkent, Almaty", "<+05>-5"},
	{"Omsk", "<+06>-6"},
	{"Novosibirsk, Bangkok", "<+07>-7"},
	{"Irkutsk, Singapore", "<+08>-8"},
	{"Beijing", "CST-8"},
	{"Tokyo", "JST-9"},
	{"Vladivostok", "<+10>-10"},
	{"New York", "EST5EDT,M3.2.0,M11.1.0"},
	{"Chicago", "CST6CDT,M3.2.0,M11.1.0"},
	{"Denver", "MST7MDT,M3.2.0,M11.1.0"},
	{"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
}

// ZoneName is a zone's name in the table, or the string itself.
func ZoneName(posix string) string {
	for _, zone := range Zones {
		if zone[1] == posix {
			return zone[0]
		}
	}
	return posix
}

// ZoneOffsets reads a POSIX TZ string's offsets from UTC, in minutes east, as clocks show them: the
// standard one, and the summer one if it has summer time. POSIX counts west, so MSK-3 is UTC+3, and a
// summer time with no offset of its own is an hour ahead.
func ZoneOffsets(posix string) (standard, summer int, hasSummer bool, err error) {
	rest, ok := skipZoneName(posix)
	if !ok {
		return 0, 0, false, fmt.Errorf("%q has no zone name", posix)
	}
	west, rest, ok := readZoneOffset(rest)
	if !ok {
		return 0, 0, false, fmt.Errorf("%q has no offset", posix)
	}
	standard = -west
	if rest == "" || rest[0] == ',' {
		return standard, 0, false, nil
	}
	if rest, ok = skipZoneName(rest); !ok {
		return 0, 0, false, fmt.Errorf("%q has no summer name", posix)
	}
	summer = standard + 60
	if summerWest, _, ok := readZoneOffset(rest); ok {
		summer = -summerWest
	}
	return standard, summer, true, nil
}

// A name of three letters or more, or any in <>.
func skipZoneName(text string) (string, bool) {
	if strings.HasPrefix(text, "<") {
		end := strings.IndexByte(text, '>')
		return text[end+1:], end > 1
	}
	letters := 0
	for letters < len(text) && (text[letters] >= 'A' && text[letters] <= 'Z' || text[letters] >= 'a' && text[letters] <= 'z') {
		letters++
	}
	return text[letters:], letters >= 3
}

// [+-]hh[:mm[:ss]], in minutes west of UTC.
func readZoneOffset(text string) (int, string, bool) {
	sign := 1
	if text != "" && (text[0] == '+' || text[0] == '-') {
		if text[0] == '-' {
			sign = -1
		}
		text = text[1:]
	}
	end := 0
	for end < len(text) && (text[end] >= '0' && text[end] <= '9' || text[end] == ':') {
		end++
	}
	parts := strings.Split(text[:end], ":")
	hours, err := strconv.Atoi(parts[0])
	if err != nil || hours > 24 {
		return 0, text, false
	}
	minutes := 0
	if len(parts) > 1 {
		if minutes, err = strconv.Atoi(parts[1]); err != nil || minutes > 59 {
			return 0, text, false
		}
	}
	return sign * (hours*60 + minutes), text[end:], true
}

// UTCOffsetText is an offset east of UTC as clocks are labelled with it: UTC+04:00, UTC-05:00.
func UTCOffsetText(minutes int) string {
	sign := '+'
	if minutes < 0 {
		sign, minutes = '-', -minutes
	}
	return fmt.Sprintf("UTC%c%02d:%02d", sign, minutes/60, minutes%60)
}

// ZoneLabel is a zone with its offset from UTC first, and its summer one after its name if it has one:
// UTC+01:00 Berlin, Paris, Warsaw, Rome (summer UTC+02:00).
func ZoneLabel(posix string) string {
	standard, summer, hasSummer, err := ZoneOffsets(posix)
	if err != nil {
		return ZoneName(posix)
	}
	label := UTCOffsetText(standard) + " " + ZoneName(posix)
	if hasSummer {
		label += " (summer " + UTCOffsetText(summer) + ")"
	}
	return label
}

var effectID = regexp.MustCompile(`^[a-z0-9-]+$`)

// EffectIDFromPath makes the id an effect would take from a file's name, as the portal says: the
// name is the id.
func EffectIDFromPath(path string) (string, error) {
	name := strings.TrimSuffix(filepath.Base(strings.ReplaceAll(path, `\`, "/")), filepath.Ext(path))
	name = strings.ToLower(strings.ReplaceAll(name, "_", "-"))
	if len(name) == 0 || len(name) > maxEffectIDBytes || !effectID.MatchString(name) {
		return "", fmt.Errorf("%q cannot be an effect's id: 1 to 32 lowercase letters, digits or hyphens", name)
	}
	return name, nil
}

// splice puts words in place of the bytes from start to end, and leaves every other byte as it was.
func splice(file []byte, start, end int, words string) []byte {
	out := make([]byte, 0, len(file)+len(words))
	out = append(out, file[:start]...)
	out = append(out, words...)
	return append(out, file[end:]...)
}

func expectDelim(decoder *json.Decoder, want json.Delim) error {
	token, err := decoder.Token()
	if err != nil {
		return fmt.Errorf("the file is not JSON: %w", err)
	}
	if token != want {
		return fmt.Errorf("the file has no %c where it should", want)
	}
	return nil
}

func skipValue(decoder *json.Decoder) error {
	var skipped json.RawMessage
	if err := decoder.Decode(&skipped); err != nil {
		return fmt.Errorf("the file is not JSON: %w", err)
	}
	return nil
}

// What sx and ix are in a layer that leaves them out.
const defaultLayerValue = 128

// ClientLabel is what the Info page will call this program.
func ClientLabel(host string) string {
	label := "csiGarland on " + runtime.GOOS
	if host != "" {
		label = "csiGarland on " + host
	}
	const maxLabelBytes = 23
	if len(label) > maxLabelBytes {
		// Not halfway through a letter of a computer's name.
		label = strings.ToValidUTF8(label[:maxLabelBytes], "")
	}
	return label
}

// LogLines is a log kept to a size, as it may run for days.
type LogLines struct {
	Lines []string
	tail  string
}

const maxLogLines = 3000

// Add takes in what the garland sent, which may end in the middle of a line.
func (l *LogLines) Add(text string, lost uint32) {
	if lost > 0 {
		l.Lines = append(l.Lines, fmt.Sprintf("... %d bytes were lost before they were read ...", lost))
	}
	parts := strings.Split(l.tail+text, "\n")
	l.tail = parts[len(parts)-1]
	for _, line := range parts[:len(parts)-1] {
		l.Lines = append(l.Lines, strings.TrimRight(line, "\r"))
	}
	l.trim()
}

func (l *LogLines) trim() {
	if extra := len(l.Lines) - maxLogLines; extra > 0 {
		l.Lines = append([]string(nil), l.Lines[extra:]...)
	}
}

// Missed notes lines the garland dropped because they came while its log was being sent.
func (l *LogLines) Missed(lines uint32) {
	if lines > 0 {
		l.Lines = append(l.Lines, fmt.Sprintf("... %d lines came while the log was being sent, and were lost ...", lines))
		l.trim()
	}
}

// Restarted notes that the garland started again, and what was left of a line will not be finished.
func (l *LogLines) Restarted() {
	if l.tail != "" {
		l.Lines = append(l.Lines, l.tail)
		l.tail = ""
	}
	l.Lines = append(l.Lines, "... the garland restarted ...")
	l.trim()
}

// Clear forgets what was shown.
func (l *LogLines) Clear() {
	l.Lines = nil
	l.tail = ""
}

// Count is how many lines there are to show, with the one not finished yet: if the garland has just
// fallen over, that is the one worth seeing.
func (l *LogLines) Count() int {
	if l.tail != "" {
		return len(l.Lines) + 1
	}
	return len(l.Lines)
}

// Line is one of Count.
func (l *LogLines) Line(i int) string {
	if i < len(l.Lines) {
		return l.Lines[i]
	}
	return l.tail
}

// Text is all of it, for copying.
func (l *LogLines) Text() string {
	return strings.Join(l.Lines, "\n") + "\n" + l.tail
}

// Kilobytes is as the portal shows memory.
func Kilobytes(bytes int64) string {
	return fmt.Sprintf("%d KB", int64(math.Round(float64(bytes)/1024)))
}

// UpdateText says what an update file holds.
func UpdateText(update api.UpdateFile) string {
	var parts []string
	if update.HasFirmware {
		version := update.Version
		if version == "" {
			version = "of unknown version"
		}
		parts = append(parts, "firmware "+version)
	}
	if update.HasEffects {
		if update.Effects < 0 {
			parts = append(parts, "a newer set of effects")
		} else {
			parts = append(parts, fmt.Sprintf("%d effects", update.Effects))
		}
	}
	return "This file holds " + strings.Join(parts, " and ") + "."
}
