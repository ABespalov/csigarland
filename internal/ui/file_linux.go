// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The system's own dialogs for choosing a file to open or to save, on Linux: the desktop
// portal's, or zenity's where there is no portal.
// Contributors: None

package ui

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"io/fs"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"gioui.org/app"
	"github.com/godbus/dbus/v5"
)

const (
	portalName      = "org.freedesktop.portal.Desktop"
	portalPath      = dbus.ObjectPath("/org/freedesktop/portal/desktop")
	portalChooser   = "org.freedesktop.portal.FileChooser"
	portalRequest   = "org.freedesktop.portal.Request"
	portalResponse  = portalRequest + ".Response"
	portalRequests  = "/org/freedesktop/portal/desktop/request/"
	portalCancelled = 1
)

var errNoFileDialog = errors.New("this system has neither the desktop portal nor zenity to choose a file with")

// errNoPortal says the portal is not there to ask, so another dialog is: not a failure of the portal.
var errNoPortal = errors.New("no desktop portal")

// viewHandle is the window's own, for a dialog to open over it rather than anywhere. Only X11 names a
// window to another process this simply: on Wayland the dialog opens on its own.
func viewHandle(event app.ViewEvent) uintptr {
	if view, isX11 := event.(app.X11ViewEvent); isX11 {
		return view.Window
	}
	return 0
}

// parentWindow is the owner as the portal names a window.
func parentWindow(owner uintptr) string {
	if owner == 0 {
		return ""
	}
	return fmt.Sprintf("x11:%x", owner)
}

// As the portal has a filter: a name, and patterns each with its kind, 0 for a glob.
type portalPattern struct {
	Kind    uint32
	Pattern string
}

type portalFilter struct {
	Name     string
	Patterns []portalPattern
}

func portalFilters(description, pattern string) []portalFilter {
	return []portalFilter{
		{Name: description, Patterns: []portalPattern{{Kind: 0, Pattern: pattern}}},
		{Name: "All files", Patterns: []portalPattern{{Kind: 0, Pattern: "*"}}},
	}
}

// requestPath is where the portal's answer comes from, known ahead of asking so that it cannot be missed:
// the bus's name for this connection, without its colon and with its dots as underscores, and the token.
func requestPath(sender, token string) dbus.ObjectPath {
	sender = strings.ReplaceAll(strings.TrimPrefix(sender, ":"), ".", "_")
	return dbus.ObjectPath(portalRequests + sender + "/" + token)
}

// pathFromURI is the file a portal's answer names. Anything but a local file is refused.
func pathFromURI(uri string) (string, error) {
	parsed, err := url.Parse(uri)
	if err != nil {
		return "", fmt.Errorf("the dialog's answer: %w", err)
	}
	if parsed.Scheme != "file" || parsed.Path == "" {
		return "", fmt.Errorf("the dialog chose %q, not a file on this computer", uri)
	}
	return filepath.FromSlash(parsed.Path), nil
}

// saveStart is a name to start a save with, as the portal takes it: the name, and its folder as bytes ending
// in a NUL, when it is a whole path, as when the dialog is asked again about a file that is there.
func saveStart(startName string) (string, []byte) {
	if !filepath.IsAbs(startName) {
		return startName, nil
	}
	return filepath.Base(startName), append([]byte(filepath.Dir(startName)), 0)
}

// withExtension adds the extension to a name chosen without one, as the dialog on Windows does.
func withExtension(path, extension string) string {
	if path == "" || extension == "" || filepath.Ext(path) != "" {
		return path
	}
	return path + "." + extension
}

func newToken() (string, error) {
	random := make([]byte, 8)
	if _, err := rand.Read(random); err != nil {
		return "", fmt.Errorf("name the dialog's request: %w", err)
	}
	return "csigarland_" + hex.EncodeToString(random), nil
}

// portalChoose asks the desktop portal, and waits for the owner, or for ctx to end, which closes the dialog.
// errNoPortal says to ask another way.
func portalChoose(ctx context.Context, owner uintptr, save bool, title, pattern, description, startName string) (string, error) {
	conn, err := dbus.ConnectSessionBus()
	if err != nil {
		return "", fmt.Errorf("%w: %w", errNoPortal, err)
	}
	defer func() { _ = conn.Close() }()
	token, err := newToken()
	if err != nil {
		return "", err
	}
	names := conn.Names()
	if len(names) == 0 {
		return "", fmt.Errorf("%w: the session bus gave no name", errNoPortal)
	}
	expected := requestPath(names[0], token)
	if err := conn.AddMatchSignal(dbus.WithMatchInterface(portalRequest), dbus.WithMatchMember("Response")); err != nil {
		return "", fmt.Errorf("%w: %w", errNoPortal, err)
	}
	signals := make(chan *dbus.Signal, 4)
	conn.Signal(signals)
	defer conn.RemoveSignal(signals)
	filters := portalFilters(description, pattern)
	options := map[string]dbus.Variant{
		"handle_token":   dbus.MakeVariant(token),
		"modal":          dbus.MakeVariant(true),
		"filters":        dbus.MakeVariant(filters),
		"current_filter": dbus.MakeVariant(filters[0]),
	}
	method := portalChooser + ".OpenFile"
	if save {
		method = portalChooser + ".SaveFile"
		name, folder := saveStart(startName)
		options["current_name"] = dbus.MakeVariant(name)
		if folder != nil {
			options["current_folder"] = dbus.MakeVariant(folder)
		}
	} else {
		options["multiple"] = dbus.MakeVariant(false)
	}
	var handle dbus.ObjectPath
	if err := conn.Object(portalName, portalPath).Call(method, 0, parentWindow(owner), title, options).Store(&handle); err != nil {
		var dbusErr dbus.Error
		if errors.As(err, &dbusErr) && strings.HasPrefix(dbusErr.Name, "org.freedesktop.DBus.Error.") {
			return "", fmt.Errorf("%w: %w", errNoPortal, err)
		}
		return "", fmt.Errorf("the system's file dialog: %w", err)
	}
	// An old portal answers from a path of its own choosing.
	for {
		select {
		case <-ctx.Done():
			// The dialog goes with the utility: the request closed, which the portal answers with nothing.
			_ = conn.Object(portalName, handle).Call(portalRequest+".Close", 0).Err
			return "", fmt.Errorf("the file dialog: %w", ctx.Err())
		case signal, open := <-signals:
			if !open {
				return "", errors.New("the session bus went away while the file dialog was open")
			}
			if signal.Name != portalResponse || (signal.Path != expected && signal.Path != handle) {
				continue
			}
			return portalAnswer(signal.Body)
		}
	}
}

// portalAnswer reads a Response: its code, and the files chosen. "" is the owner cancelling.
func portalAnswer(body []any) (string, error) {
	if len(body) < 2 {
		return "", errors.New("the file dialog answered nothing")
	}
	code, _ := body[0].(uint32)
	if code == portalCancelled {
		return "", nil
	}
	if code != 0 {
		return "", errors.New("the system's file dialog failed")
	}
	results, _ := body[1].(map[string]dbus.Variant)
	uris, _ := results["uris"].Value().([]string)
	if len(uris) == 0 {
		return "", nil
	}
	return pathFromURI(uris[0])
}

func zenityArgs(save bool, title, pattern, description, startName string) []string {
	args := []string{"--file-selection", "--title=" + title,
		"--file-filter=" + description + " | " + pattern, "--file-filter=All files | *"}
	if save {
		args = append(args, "--save", "--confirm-overwrite")
		if startName != "" {
			args = append(args, "--filename="+startName)
		}
	}
	return args
}

// zenityChoose asks zenity, where the desktop has no portal. Its exit code 1 is the owner cancelling; ctx ending
// ends zenity.
func zenityChoose(ctx context.Context, save bool, title, pattern, description, startName string) (string, error) {
	output, err := exec.CommandContext(ctx, "zenity", zenityArgs(save, title, pattern, description, startName)...).Output()
	if ctxErr := ctx.Err(); ctxErr != nil {
		return "", fmt.Errorf("the file dialog: %w", ctxErr)
	}
	var exit *exec.ExitError
	switch {
	case errors.Is(err, exec.ErrNotFound):
		return "", errNoFileDialog
	case errors.As(err, &exit) && exit.ExitCode() == 1:
		return "", nil
	case err != nil:
		return "", fmt.Errorf("zenity's file dialog: %w", err)
	}
	return strings.TrimRight(string(output), "\r\n"), nil
}

func runFileDialog(ctx context.Context, owner uintptr, save bool, title, pattern, description, startName string) (string, error) {
	path, err := portalChoose(ctx, owner, save, title, pattern, description, startName)
	if errors.Is(err, errNoPortal) {
		return zenityChoose(ctx, save, title, pattern, description, startName)
	}
	return path, err
}

// chooseFile asks, over the window whose handle is owner, for a file to open, and answers "" if the
// owner cancelled. It waits, so call it away from the window's goroutine; ctx ending closes the dialog.
func chooseFile(ctx context.Context, owner uintptr, title, pattern, description string) (string, error) {
	return runFileDialog(ctx, owner, false, title, pattern, description, "")
}

// saveDialog asks once where to save; a variable, so that a test can stand in for the system's dialog.
var saveDialog = func(ctx context.Context, owner uintptr, title, pattern, description, startName string) (string, error) {
	return runFileDialog(ctx, owner, true, title, pattern, description, startName)
}

// chooseSaveFile asks where to save a file, as chooseFile does. A name given without the extension gets it,
// as the dialog on Windows gives it, but the dialog asked about replacing the name typed, not the one with
// the extension: so where that one is there, the dialog is asked again with it, and asks about it.
func chooseSaveFile(ctx context.Context, owner uintptr, title, pattern, description, extension, startName string) (string, error) {
	for {
		if err := ctx.Err(); err != nil {
			return "", fmt.Errorf("the file dialog: %w", err)
		}
		path, err := saveDialog(ctx, owner, title, pattern, description, startName)
		if err != nil || path == "" {
			return "", err
		}
		final := withExtension(path, extension)
		if final == path {
			return path, nil
		}
		if _, err := os.Lstat(final); errors.Is(err, fs.ErrNotExist) {
			return final, nil
		}
		startName = final
	}
}
