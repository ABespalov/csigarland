// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: Tests of a backup made from one garland and put back on another that was erased.
// Contributors: None

package backup

import (
	"context"
	"encoding/json"
	"encoding/pem"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"reflect"
	"slices"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/ABespalov/csigarland/internal/api"
)

// garland is a garland's choices in memory, behind the routes a backup uses.
type garland struct {
	mu        sync.Mutex
	schedule  []api.Action
	revision  uint32
	favorites map[string]bool
	uploads   map[string]string
	playlist  api.Playlist
	display   api.Display
	// The power settings, with their totals; nil as a garland of 1.1.1 or before, which answers 404.
	power *api.PowerBudget
	// When not 0, what GET playlist answers instead.
	playlistStatus int
	// What GET firmware says, 1.0.0.103 when empty.
	version string
	// What GET schedule says the actions name that the garland lacks.
	missing []string
	// When set, what happens on the garland as a route is asked, before its answer.
	meanwhile func(g *garland, route string)
	// What GET effects says of the list's being whole: nil says nothing, as a garland of 1.2.0 or before.
	complete *bool
}

var builtIn = []string{"aurora", "fireplace", "candle"}

func (g *garland) effects() []api.Effect {
	var list []api.Effect
	for _, id := range builtIn {
		list = append(list, api.Effect{ID: id, Name: id, Favorite: g.favorites[id], BuiltIn: true})
	}
	for id := range g.uploads {
		list = append(list, api.Effect{ID: id, Name: id, Favorite: g.favorites[id], Uploaded: true})
	}
	slices.SortFunc(list, func(a, b api.Effect) int { return strings.Compare(a.ID, b.ID) })
	return list
}

func (g *garland) serve(t *testing.T) *api.Client {
	t.Helper()
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		g.mu.Lock()
		defer g.mu.Unlock()
		reply := func(value any) {
			if err := json.NewEncoder(w).Encode(value); err != nil {
				t.Errorf("answer: %v", err)
			}
		}
		body, _ := io.ReadAll(r.Body)
		var fields map[string]json.RawMessage
		_ = json.Unmarshal(body, &fields)
		route := strings.TrimPrefix(r.URL.Path, "/api/v1/")
		if g.meanwhile != nil {
			g.meanwhile(g, r.Method+" "+route)
		}
		switch r.Method + " " + route {
		case "GET firmware":
			version := g.version
			if version == "" {
				version = "1.0.0.103"
			}
			reply(api.Firmware{Version: version})
		case "GET schedule":
			reply(api.Schedule{Entries: g.schedule, Max: 32, Revision: g.revision, Missing: g.missing})
		case "POST schedule":
			var sent struct {
				Entries  []api.Action `json:"entries"`
				Revision uint32       `json:"revision"`
			}
			_ = json.Unmarshal(body, &sent)
			if sent.Revision != g.revision {
				http.Error(w, "changed", http.StatusConflict)
				return
			}
			g.schedule, g.revision = sent.Entries, g.revision+1
			reply(api.Schedule{Entries: g.schedule, Max: 32, Revision: g.revision})
		case "GET effects":
			answer := map[string]any{"effects": g.effects()}
			if g.complete != nil {
				answer["complete"] = *g.complete
			}
			reply(answer)
		case "GET effects/file":
			// As the garland sends it: the file as kept, or 404 for one it does not hold.
			file, held := g.uploads[r.URL.Query().Get("id")]
			if !held {
				http.NotFound(w, r)
				return
			}
			_, _ = io.WriteString(w, file)
		case "POST effects/file":
			g.uploads[r.URL.Query().Get("id")] = string(body)
			reply(map[string]bool{"ok": true})
		case "POST effects/favorite":
			var id string
			var favorite bool
			_ = json.Unmarshal(fields["id"], &id)
			_ = json.Unmarshal(fields["favorite"], &favorite)
			g.favorites[id] = favorite
			reply(map[string]bool{"ok": true})
		case "GET playlist":
			if g.playlistStatus != 0 {
				http.Error(w, "no playlist", g.playlistStatus)
				return
			}
			reply(g.playlist)
		case "POST playlist":
			_ = json.Unmarshal(body, &g.playlist)
			reply(g.playlist)
		case "GET display":
			reply(g.display)
		case "POST display":
			_ = json.Unmarshal(body, &g.display)
			reply(g.display)
		case "GET leds/power":
			if g.power == nil {
				http.NotFound(w, r)
				return
			}
			reply(g.power)
		case "POST leds/power":
			if g.power == nil {
				http.NotFound(w, r)
				return
			}
			_ = json.Unmarshal(body, g.power)
			reply(g.power)
		default:
			http.NotFound(w, r)
		}
	}))
	t.Cleanup(server.Close)
	root := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: server.Certificate().Raw})
	client, err := api.New(server.URL, root, "a-token")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(client.Close)
	return client
}

func TestABackupPutsAnErasedGarlandBack(t *testing.T) {
	from := &garland{
		schedule:  []api.Action{{Enabled: true, Time: "07:00", Days: 62, Power: api.StepRise, Effect: "my-snow"}, {Enabled: true, Time: "23:30", Days: 127, Power: api.StepOff}},
		revision:  4,
		favorites: map[string]bool{"aurora": true, "my-snow": true},
		uploads:   map[string]string{"my-snow": `{"format":"csigarland.effect/1","name":"My Snow","layers":[]}`},
		playlist:  api.Playlist{Effects: []string{"my-snow", "aurora"}, Dwell: 90, CrossFade: 2000, Max: 16},
		display:   api.Display{Saver: "flying-clock", Delay: 120, ScreenBrightness: 80, SaverBrightness: 20},
	}
	ctx := context.Background()
	made, err := Collect(ctx, from.serve(t), time.Date(2026, 11, 3, 12, 0, 0, 0, time.UTC))
	if err != nil {
		t.Fatal(err)
	}
	data, err := Encode(made)
	if err != nil {
		t.Fatal(err)
	}
	read, err := Parse(data)
	if err != nil {
		t.Fatal(err)
	}
	if read.Firmware != "1.0.0.103" || read.Made != "2026-11-03T12:00:00Z" || len(read.Uploads) != 1 {
		t.Fatalf("%+v", read)
	}

	// Erased: the defaults' favourite, nothing uploaded, no list.
	to := &garland{revision: 1, favorites: map[string]bool{"candle": true}, uploads: map[string]string{}, display: api.Display{Saver: "starfield", Delay: 60}}
	var steps []string
	if err := Restore(ctx, to.serve(t), read, func(step string) { steps = append(steps, step) }); err != nil {
		t.Fatalf("%v, after %v", err, steps)
	}
	if !reflect.DeepEqual(to.schedule, from.schedule) || to.uploads["my-snow"] != from.uploads["my-snow"] {
		t.Fatalf("schedule %+v, uploads %v", to.schedule, to.uploads)
	}
	if !to.favorites["aurora"] || !to.favorites["my-snow"] || to.favorites["candle"] {
		t.Fatalf("favourites %v", to.favorites)
	}
	if !reflect.DeepEqual(to.playlist.Effects, from.playlist.Effects) || to.playlist.Dwell != 90 || to.display.Saver != "flying-clock" {
		t.Fatalf("playlist %+v, screen %+v", to.playlist, to.display)
	}
}

// The power settings go with the rest, the four the owner sets and not what they come to; a garland of a
// release with them that does not answer fails the backup, as a 1.1.1 garland's 404 does not.
func TestThePowerSettingsAreKept(t *testing.T) {
	from := &garland{version: "1.2.0.120", favorites: map[string]bool{"aurora": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"},
		power: &api.PowerBudget{SupplyMilliamps: 5000, WhiteMilliamps: 36, IdleMilliamps: 1, MarginPercent: 10, LimitMilliamps: 4275, Leds: 32}}
	ctx := context.Background()
	made, err := Collect(ctx, from.serve(t), time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if made.Power == nil || *made.Power != (api.PowerBudget{SupplyMilliamps: 5000, WhiteMilliamps: 36, IdleMilliamps: 1, MarginPercent: 10}) {
		t.Fatalf("%+v", made.Power)
	}
	data, err := Encode(made)
	if err != nil {
		t.Fatal(err)
	}
	read, err := Parse(data)
	if err != nil {
		t.Fatal(err)
	}
	to := &garland{revision: 1, favorites: map[string]bool{"candle": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"}, power: &api.PowerBudget{SupplyMilliamps: 2000, WhiteMilliamps: 40, IdleMilliamps: 1, MarginPercent: 20}}
	if err := Restore(ctx, to.serve(t), read, func(string) {}); err != nil {
		t.Fatal(err)
	}
	if to.power.SupplyMilliamps != 5000 || to.power.WhiteMilliamps != 36 || to.power.MarginPercent != 10 {
		t.Fatalf("%+v", to.power)
	}
	// A backup from before the settings leaves a garland's own as they are.
	old := read
	old.Power = nil
	kept := &garland{revision: 1, favorites: map[string]bool{"candle": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"}, power: &api.PowerBudget{SupplyMilliamps: 3000, WhiteMilliamps: 40, IdleMilliamps: 1, MarginPercent: 20}}
	if err := Restore(ctx, kept.serve(t), old, func(string) {}); err != nil || kept.power.SupplyMilliamps != 3000 {
		t.Fatalf("%v %+v", err, kept.power)
	}
	from.power = nil
	if _, err := Collect(ctx, from.serve(t), time.Now()); err == nil || !strings.Contains(err.Error(), "power") {
		t.Fatalf("a 1.2.0 garland without power settings: %v", err)
	}
}

func TestOnlyABackupIsRead(t *testing.T) {
	for _, bad := range []string{
		`{"format":"something/1"}`,
		`{"format":"csigarland.backup/1","extra":1}`,
		`{"format":"csigarland.backup/1"} {}`,
		// More() says no before a closing bracket: a backup whole and one bracket after it is no backup.
		`{"format":"csigarland.backup/1","made":"x","firmware":"1.1.0.103","schedule":[],"favorites":[],"uploads":[],"display":{"saver":"none"}}]`,
		`{"format":"csigarland.backup/1","made":"x","firmware":"1.1.0.103","schedule":[],"favorites":[],"uploads":[],"display":{"saver":"none"}}}`,
		`{"format":"csigarland.backup/1","uploads":[{"id":"","file":"{}"}]}`,
		// A part left out is not an empty one: it would replace what the garland holds with nothing.
		`{"format":"csigarland.backup/1"}`,
		`{"format":"csigarland.backup/1","made":"x","firmware":"1.1.0.103","favorites":[],"uploads":[],"display":{"saver":"none"}}`,
		`not json`,
	} {
		if _, err := Parse([]byte(bad)); err == nil {
			t.Errorf("read %s", bad)
		}
	}
	if _, err := Parse(make([]byte, MaxBytes+1)); err == nil {
		t.Error("read a file past the bound")
	}
	// Space after it is still the end.
	whole := `{"format":"csigarland.backup/1","made":"x","firmware":"1.1.0.103","schedule":[],"favorites":[],"uploads":[],"display":{"saver":"none"}}` + "\n \n"
	if _, err := Parse([]byte(whole)); err != nil {
		t.Errorf("a whole backup with space after it: %v", err)
	}
}

// An upload replaced under the same id while a backup is read changes nothing else the API says: the
// backup still reads it again, so it never pairs one effect's old file with another's new one.
func TestABackupSeesAnUploadReplacedUnderItsId(t *testing.T) {
	from := &garland{revision: 1, favorites: map[string]bool{"aurora": true}, display: api.Display{Saver: "none"},
		uploads:  map[string]string{"a": `{"v":1}`, "b": `{"v":1}`},
		playlist: api.Playlist{Effects: []string{"a", "b"}, Dwell: 60, CrossFade: 1000, Max: 16}}
	// After a's file is read, and before b's: both replaced, as another program saving A2 then B2.
	replaced := false
	reads := 0
	from.meanwhile = func(g *garland, route string) {
		if route != "GET effects/file" {
			return
		}
		reads++
		if reads == 2 && !replaced {
			replaced = true
			g.uploads["a"], g.uploads["b"] = `{"v":2}`, `{"v":2}`
		}
	}
	made, err := Collect(context.Background(), from.serve(t), time.Now())
	if err != nil || !replaced {
		t.Fatalf("%v, replaced %v", err, replaced)
	}
	for _, upload := range made.Uploads {
		if upload.File != `{"v":2}` {
			t.Fatalf("the backup pairs files of two moments: %+v", made.Uploads)
		}
	}
}

// A garland of 1.0.0 has no playlist: its backup leaves it out. One that failed to answer makes no backup.
func TestOnlyAGarlandWithNoPlaylistMakesABackupWithout(t *testing.T) {
	for status, made := range map[int]bool{http.StatusNotFound: true, http.StatusServiceUnavailable: false} {
		from := &garland{favorites: map[string]bool{"aurora": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"}, playlistStatus: status}
		backup, err := Collect(context.Background(), from.serve(t), time.Now())
		if (err == nil) != made || backup.Playlist != nil {
			t.Errorf("answered %d: %v, playlist %v", status, err, backup.Playlist)
		}
	}
}

// A schedule saved while the rest is put back stays: the restore's own fails.
func TestARestoreDoesNotOverwriteAScheduleSavedMeanwhile(t *testing.T) {
	read := Backup{Format: Format, Made: "x", Firmware: "1.0.0.103", Schedule: []api.Action{{Enabled: true, Time: "07:00", Days: 127, Power: api.StepOn}},
		Favorites: []string{"aurora"}, Uploads: []Upload{}, Display: api.Display{Saver: "none"}}
	saved := []api.Action{{Enabled: true, Time: "22:00", Days: 127, Power: api.StepOff}}
	to := &garland{revision: 1, favorites: map[string]bool{"candle": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"}}
	to.meanwhile = func(g *garland, route string) {
		if route == "POST effects/favorite" && g.revision == 1 {
			g.schedule, g.revision = saved, 2
		}
	}
	err := Restore(context.Background(), to.serve(t), read, func(string) {})
	if err == nil || !reflect.DeepEqual(to.schedule, saved) {
		t.Fatalf("%v, schedule %+v", err, to.schedule)
	}
}

// A garland changed by another page while a backup is read is read again, and a backup whose schedule or
// playlist names an effect it does not hold is refused.
func TestABackupIsOnePictureThatHoldsWhatItNames(t *testing.T) {
	from := &garland{revision: 1, favorites: map[string]bool{"aurora": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"},
		playlist: api.Playlist{Effects: []string{"aurora"}, Dwell: 60, CrossFade: 1000, Max: 16}}
	changed := false
	from.meanwhile = func(g *garland, route string) {
		if route == "GET display" && !changed {
			changed = true
			g.revision++
		}
	}
	made, err := Collect(context.Background(), from.serve(t), time.Now())
	if err != nil || !changed || made.Playlist == nil {
		t.Fatalf("%v, changed %v", err, changed)
	}
	from.meanwhile = nil
	from.schedule = []api.Action{{Enabled: true, Time: "07:00", Days: 127, Effect: "gone"}}
	if _, err := Collect(context.Background(), from.serve(t), time.Now()); err == nil || !strings.Contains(err.Error(), "gone") {
		t.Fatalf("a schedule naming an effect not there was kept: %v", err)
	}
}

// A garland of a release with a playlist that answers 404 for it makes no backup: only 1.0.0 has none.
func TestAGarlandOfAReleaseWithAPlaylistMustGiveIt(t *testing.T) {
	from := &garland{version: "1.1.0.106", favorites: map[string]bool{"aurora": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"},
		playlistStatus: http.StatusNotFound}
	if _, err := Collect(context.Background(), from.serve(t), time.Now()); err == nil {
		t.Fatal("a 1.1.0 garland with no playlist made a backup without it")
	}
}

// A list of effects the garland says is not whole, as when an upload it holds could not be read yet, makes
// no backup, though two readings of it agree; a list that says it is whole, or says nothing, does.
func TestAListNotWholeMakesNoBackup(t *testing.T) {
	partial, whole := false, true
	from := &garland{favorites: map[string]bool{"aurora": true}, uploads: map[string]string{"b": `{"v":1}`}, display: api.Display{Saver: "none"},
		complete: &partial}
	if _, err := Collect(context.Background(), from.serve(t), time.Now()); !errors.Is(err, ErrIncomplete) {
		t.Fatalf("a partial list made a backup: %v", err)
	}
	from.complete = &whole
	if made, err := Collect(context.Background(), from.serve(t), time.Now()); err != nil || len(made.Uploads) != 1 {
		t.Fatalf("%v, uploads %+v", err, made.Uploads)
	}
	// Whole at the first reading and not at the second, read to check it: none either.
	from.meanwhile = func(g *garland, route string) {
		if route == "GET effects/file" {
			g.complete = &partial
		}
	}
	if _, err := Collect(context.Background(), from.serve(t), time.Now()); !errors.Is(err, ErrIncomplete) {
		t.Fatalf("a list partial at its check made a backup: %v", err)
	}
}

// An upload deleted between the list of effects and its file is a change of the garland: read again, the
// backup is made of what is there.
func TestAnUploadDeletedWhileReadIsReadAgain(t *testing.T) {
	from := &garland{favorites: map[string]bool{"aurora": true}, uploads: map[string]string{"a": `{"v":1}`, "b": `{"v":1}`}, display: api.Display{Saver: "none"}}
	deleted := false
	from.meanwhile = func(g *garland, route string) {
		if route == "GET effects/file" && !deleted {
			deleted = true
			delete(g.uploads, "a")
		}
	}
	made, err := Collect(context.Background(), from.serve(t), time.Now())
	if err != nil || !deleted || len(made.Uploads) != 1 || made.Uploads[0].ID != "b" {
		t.Fatalf("%v, uploads %+v", err, made.Uploads)
	}
}

// A playlist that keeps an effect its garland lacks is backed up as it is, and put back on another
// garland without it, which is said.
func TestAPlaylistKeepsAnEffectItsGarlandLacks(t *testing.T) {
	from := &garland{version: "1.1.0.106", favorites: map[string]bool{"aurora": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"},
		playlist: api.Playlist{Effects: []string{"gone", "aurora"}, Missing: []string{"gone"}, Dwell: 60, CrossFade: 1000, Max: 16}}
	made, err := Collect(context.Background(), from.serve(t), time.Now())
	if err != nil || made.Playlist == nil || !slices.Equal(made.Playlist.Effects, []string{"gone", "aurora"}) {
		t.Fatalf("%v, playlist %+v", err, made.Playlist)
	}
	to := &garland{revision: 1, favorites: map[string]bool{"candle": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"}}
	err = Restore(context.Background(), to.serve(t), made, func(string) {})
	if err == nil || !strings.Contains(err.Error(), "left out gone") || !slices.Equal(to.playlist.Effects, []string{"aurora"}) {
		t.Fatalf("%v, playlist put back %+v", err, to.playlist)
	}
}

// A schedule keeping an action whose effect its garland lacks is backed up whole. Put back on a garland
// without it, that action is left out and said; on the same garland, which keeps it, it stays.
func TestAScheduleKeepsAnEffectItsGarlandLacks(t *testing.T) {
	gone := api.Action{Enabled: true, Time: "06:30", Days: 127, Brightness: 40, Effect: "gone"}
	off := api.Action{Enabled: true, Time: "23:00", Days: 127, Power: api.StepOff}
	from := &garland{version: "1.1.0.106", revision: 4, schedule: []api.Action{gone, off}, missing: []string{"gone"},
		favorites: map[string]bool{"aurora": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"}}
	made, err := Collect(context.Background(), from.serve(t), time.Now())
	if err != nil || len(made.Schedule) != 2 || !slices.Equal(made.ScheduleMissing, []string{"gone"}) {
		t.Fatalf("%v, schedule %+v, missing %v", err, made.Schedule, made.ScheduleMissing)
	}
	to := &garland{revision: 1, favorites: map[string]bool{"candle": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"}}
	err = Restore(context.Background(), to.serve(t), made, func(string) {})
	if err == nil || !strings.Contains(err.Error(), "naming gone") || !reflect.DeepEqual(to.schedule, []api.Action{off}) {
		t.Fatalf("%v, schedule put back %+v", err, to.schedule)
	}
	if err := Restore(context.Background(), from.serve(t), made, func(string) {}); err != nil || len(from.schedule) != 2 {
		t.Fatalf("%v, the same garland's schedule %+v", err, from.schedule)
	}
}

// A garland that lacks every favourite of a backup keeps its own, and still takes the playlist's effects
// it has: the favourites left alone are no catalog unknown.
func TestARestoreWithNoFavouriteHereStillPutsBackThePlaylist(t *testing.T) {
	made := Backup{Format: Format, Made: "x", Firmware: "1.1.0.106", Schedule: []api.Action{}, Favorites: []string{"gone-favourite"}, Uploads: []Upload{},
		Display: api.Display{Saver: "none"}, Playlist: &api.Playlist{Effects: []string{"gone", "aurora"}, Dwell: 60, CrossFade: 1000}}
	to := &garland{revision: 1, favorites: map[string]bool{"candle": true}, uploads: map[string]string{}, display: api.Display{Saver: "none"}}
	err := Restore(context.Background(), to.serve(t), made, func(string) {})
	if err == nil || !strings.Contains(err.Error(), "left out gone") || !slices.Equal(to.playlist.Effects, []string{"aurora"}) || !to.favorites["candle"] {
		t.Fatalf("%v, playlist %+v, favourites %v", err, to.playlist, to.favorites)
	}
}
