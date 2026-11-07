// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The system's own dialogs for choosing a file to open or to save, on macOS: AppKit's panels,
// as a sheet over the utility's window.
// Contributors: None

package ui

/*
#cgo CFLAGS: -x objective-c -fobjc-arc
#cgo LDFLAGS: -framework AppKit -framework UniformTypeIdentifiers
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

static NSString *csg_text(const char *text) {
	if (text == NULL || text[0] == 0) {
		return nil;
	}
	return [NSString stringWithUTF8String:text];
}

// The window whose content is the view owner names, or nil. Only pointers are compared: the view may
// be gone by now, and is never sent a message.
static NSWindow *csg_owner_window(uintptr_t owner) {
	if (owner == 0) {
		return nil;
	}
	for (NSWindow *window in [NSApp windows]) {
		if ((uintptr_t)(__bridge void *)window.contentView == owner && window.visible) {
			return window;
		}
	}
	return nil;
}

// Asks for a file, over the owner's window where it can, and waits for the answer. Answers 1 with *path
// set to a buffer of malloc's when a file was chosen, 0 when the owner cancelled, and -1 when called on
// the main thread, where the panel itself runs and waiting would hold it up for good.
static int csg_choose(uintptr_t owner, int save, const char *title_text, const char *extension_text,
		const char *folder_text, const char *name_text, char **path) {
	*path = NULL;
	if ([NSThread isMainThread]) {
		return -1;
	}
	NSString *title = csg_text(title_text);
	NSString *extension = csg_text(extension_text);
	NSString *folder = csg_text(folder_text);
	NSString *name = csg_text(name_text);
	__block char *answer = NULL;
	dispatch_semaphore_t done = dispatch_semaphore_create(0);
	dispatch_async(dispatch_get_main_queue(), ^{
		NSSavePanel *panel;
		if (save) {
			panel = [NSSavePanel savePanel];
			panel.canCreateDirectories = YES;
			panel.extensionHidden = NO;
			if (name != nil) {
				panel.nameFieldStringValue = name;
			}
		} else {
			NSOpenPanel *open = [NSOpenPanel openPanel];
			open.canChooseFiles = YES;
			open.canChooseDirectories = NO;
			open.allowsMultipleSelection = NO;
			panel = open;
		}
		if (title != nil) {
			panel.title = title;
			panel.message = title;
		}
		if (extension != nil) {
			UTType *type = [UTType typeWithFilenameExtension:extension];
			if (type != nil) {
				panel.allowedContentTypes = @[type];
			}
		}
		if (folder != nil) {
			panel.directoryURL = [NSURL fileURLWithPath:folder isDirectory:YES];
		}
		void (^finish)(NSModalResponse) = ^(NSModalResponse response) {
			NSURL *chosen = panel.URL;
			if (response == NSModalResponseOK && chosen != nil && chosen.fileURL) {
				answer = strdup(chosen.fileSystemRepresentation);
			}
			dispatch_semaphore_signal(done);
		};
		NSWindow *window = csg_owner_window(owner);
		if (window != nil) {
			[panel beginSheetModalForWindow:window completionHandler:finish];
		} else {
			[NSApp activateIgnoringOtherApps:YES];
			finish([panel runModal]);
		}
	});
	dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
	*path = answer;
	return answer != NULL ? 1 : 0;
}
*/
import "C"

import (
	"context"
	"errors"
	"fmt"
	"path/filepath"
	"strings"
	"unsafe"

	"gioui.org/app"
)

var errNoFileDialog = errors.New("a file dialog cannot be opened from the window's own thread")

// viewHandle is the window's own view, for a dialog to open as a sheet over its window.
func viewHandle(event app.ViewEvent) uintptr {
	if view, isAppKit := event.(app.AppKitViewEvent); isAppKit {
		return view.View
	}
	return 0
}

// patternExtension is the extension a pattern such as "*.json" names, or "" for any file.
func patternExtension(pattern string) string {
	extension, found := strings.CutPrefix(pattern, "*.")
	if !found || extension == "" || strings.ContainsAny(extension, "*?/") {
		return ""
	}
	return extension
}

// saveStart splits a name to start a save with into its folder, "" for the panel's own choice, and name.
func saveStart(startName string) (folder, name string) {
	if startName == "" || !filepath.IsAbs(startName) {
		return "", startName
	}
	return filepath.Dir(startName), filepath.Base(startName)
}

func runPanel(owner uintptr, save bool, title, extension, folder, name string) (string, error) {
	texts := make([]*C.char, 0, 4)
	defer func() {
		for _, text := range texts {
			C.free(unsafe.Pointer(text))
		}
	}()
	text := func(value string) *C.char {
		made := C.CString(value)
		texts = append(texts, made)
		return made
	}
	asSave := C.int(0)
	if save {
		asSave = 1
	}
	var path *C.char
	result := C.csg_choose(C.uintptr_t(owner), asSave, text(title), text(extension), text(folder), text(name), &path)
	if path != nil {
		defer C.free(unsafe.Pointer(path))
	}
	switch result {
	case -1:
		return "", errNoFileDialog
	case 1:
		return C.GoString(path), nil
	}
	return "", nil
}

// chooseFile asks, over the window whose handle is owner, for a file to open, and answers "" if the
// owner cancelled. It waits, so call it away from the window's goroutine. A ctx ended already opens none;
// once open, the panel is a sheet of the window and closes with it.
func chooseFile(ctx context.Context, owner uintptr, title, pattern, _ string) (string, error) {
	if err := ctx.Err(); err != nil {
		return "", fmt.Errorf("the file dialog: %w", err)
	}
	return runPanel(owner, false, title, patternExtension(pattern), "", "")
}

// chooseSaveFile asks where to save a file, as chooseFile does. The panel gives a name the extension and
// asks about replacing the file of that name itself.
func chooseSaveFile(ctx context.Context, owner uintptr, title, pattern, _, extension, startName string) (string, error) {
	if err := ctx.Err(); err != nil {
		return "", fmt.Errorf("the file dialog: %w", err)
	}
	if extension == "" {
		extension = patternExtension(pattern)
	}
	folder, name := saveStart(startName)
	return runPanel(owner, true, title, extension, folder, name)
}
