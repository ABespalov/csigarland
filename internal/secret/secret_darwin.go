// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The token store on macOS: the login keychain, through the Security framework.
// Contributors: None

package secret

/*
#cgo LDFLAGS: -framework CoreFoundation -framework Security
#include <stdlib.h>
#include <string.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

static void csg_release(CFTypeRef ref) {
	if (ref != NULL) {
		CFRelease(ref);
	}
}

// The item of one token, found by its class, service and account. NULL if it could not be made.
static CFMutableDictionaryRef csg_query(CFStringRef service, CFStringRef account) {
	CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (query != NULL) {
		CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
		CFDictionarySetValue(query, kSecAttrService, service);
		CFDictionarySetValue(query, kSecAttrAccount, account);
	}
	return query;
}

static CFStringRef csg_string(const char *text) {
	return CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
}

// Replaces the item's value, or adds the item where there is none.
static OSStatus csg_set(const char *service_text, const char *account_text, const UInt8 *bytes, CFIndex length) {
	CFStringRef service = csg_string(service_text);
	CFStringRef account = csg_string(account_text);
	CFDataRef value = CFDataCreate(kCFAllocatorDefault, bytes, length);
	CFMutableDictionaryRef query = NULL;
	CFMutableDictionaryRef change = NULL;
	OSStatus status = errSecAllocate;
	if (service != NULL && account != NULL && value != NULL) {
		query = csg_query(service, account);
		change = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
			&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	}
	if (query != NULL && change != NULL) {
		CFDictionarySetValue(change, kSecValueData, value);
		status = SecItemUpdate(query, change);
		if (status == errSecItemNotFound) {
			CFDictionarySetValue(query, kSecAttrLabel, service);
			CFDictionarySetValue(query, kSecValueData, value);
			status = SecItemAdd(query, NULL);
		}
	}
	csg_release(change);
	csg_release(query);
	csg_release(value);
	csg_release(account);
	csg_release(service);
	return status;
}

// Reads the item's value into a buffer of malloc's, which the caller frees.
static OSStatus csg_get(const char *service_text, const char *account_text, void **bytes, CFIndex *length) {
	CFStringRef service = csg_string(service_text);
	CFStringRef account = csg_string(account_text);
	CFMutableDictionaryRef query = NULL;
	CFTypeRef found = NULL;
	OSStatus status = errSecAllocate;
	*bytes = NULL;
	*length = 0;
	if (service != NULL && account != NULL) {
		query = csg_query(service, account);
	}
	if (query != NULL) {
		CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
		CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
		status = SecItemCopyMatching(query, &found);
	}
	if (status == errSecSuccess) {
		if (found == NULL || CFGetTypeID(found) != CFDataGetTypeID()) {
			status = errSecDecode;
		} else {
			CFIndex size = CFDataGetLength((CFDataRef)found);
			*bytes = malloc(size > 0 ? (size_t)size : 1);
			if (*bytes == NULL) {
				status = errSecAllocate;
			} else {
				memcpy(*bytes, CFDataGetBytePtr((CFDataRef)found), (size_t)size);
				*length = size;
			}
		}
	}
	csg_release(found);
	csg_release(query);
	csg_release(account);
	csg_release(service);
	return status;
}

static OSStatus csg_delete(const char *service_text, const char *account_text) {
	CFStringRef service = csg_string(service_text);
	CFStringRef account = csg_string(account_text);
	CFMutableDictionaryRef query = NULL;
	OSStatus status = errSecAllocate;
	if (service != NULL && account != NULL) {
		query = csg_query(service, account);
	}
	if (query != NULL) {
		status = SecItemDelete(query);
	}
	csg_release(query);
	csg_release(account);
	csg_release(service);
	return status;
}

// What the system says a status means, in a buffer of malloc's, or NULL.
static char *csg_message(OSStatus status) {
	CFStringRef message = SecCopyErrorMessageString(status, NULL);
	char *text = NULL;
	if (message != NULL) {
		CFIndex size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(message), kCFStringEncodingUTF8) + 1;
		text = malloc((size_t)size);
		if (text != NULL && !CFStringGetCString(message, text, size, kCFStringEncodingUTF8)) {
			free(text);
			text = NULL;
		}
		CFRelease(message);
	}
	return text;
}
*/
import "C"

import (
	"errors"
	"fmt"
	"sync"
	"unsafe"
)

// The account of every item: the service, as the target on Windows, names the garland.
const keychainAccount = "csiGarland"

// One call of the keychain at a time: two at once could each ask the owner to allow the utility.
var storeLock sync.Mutex

// statusError turns what the Security framework answered into an error, nil for success.
func statusError(status C.OSStatus) error {
	switch status {
	case C.errSecSuccess:
		return nil
	case C.errSecItemNotFound:
		return ErrNotFound
	case C.errSecNoDefaultKeychain, C.errSecNotAvailable:
		// A session with no keychain to keep anything in, as a Mac reached only over SSH can be: no store,
		// as on a Linux with no Secret Service, so the utility pairs for as long as it runs.
		return fmt.Errorf("%w: no keychain in this session (error %d)", ErrUnsupported, int32(status))
	}
	said := "error " + fmt.Sprint(int32(status))
	if text := C.csg_message(status); text != nil {
		said = C.GoString(text) + " (" + said + ")"
		C.free(unsafe.Pointer(text))
	}
	return errors.New(said)
}

// withNames calls the keychain with the item's service and account as C strings.
func withNames(name string, call func(service, account *C.char) C.OSStatus) error {
	service := C.CString(targetPrefix + name)
	defer C.free(unsafe.Pointer(service))
	account := C.CString(keychainAccount)
	defer C.free(unsafe.Pointer(account))
	return statusError(call(service, account))
}

// Set keeps a token under a name, replacing what was there.
func Set(name, token string) error {
	if token == "" {
		return errors.New("no token to keep")
	}
	storeLock.Lock()
	defer storeLock.Unlock()
	value := []byte(token)
	err := withNames(name, func(service, account *C.char) C.OSStatus {
		return C.csg_set(service, account, (*C.UInt8)(unsafe.Pointer(&value[0])), C.CFIndex(len(value)))
	})
	if err != nil {
		return fmt.Errorf("keep the token in the keychain: %w", err)
	}
	return nil
}

// Get reads the token kept under a name, or ErrNotFound. The system may ask the owner first whether the
// utility may read it, as after an update, whose signature differs from the one that kept it.
func Get(name string) (string, error) {
	storeLock.Lock()
	defer storeLock.Unlock()
	var bytes unsafe.Pointer
	var length C.CFIndex
	err := withNames(name, func(service, account *C.char) C.OSStatus {
		return C.csg_get(service, account, &bytes, &length)
	})
	if bytes != nil {
		defer C.free(bytes)
	}
	if err != nil {
		if errors.Is(err, ErrNotFound) {
			return "", err
		}
		return "", fmt.Errorf("read the token from the keychain: %w", err)
	}
	return string(C.GoBytes(bytes, C.int(length))), nil
}

// Delete forgets the token under a name. One not there is no failure.
func Delete(name string) error {
	storeLock.Lock()
	defer storeLock.Unlock()
	err := withNames(name, func(service, account *C.char) C.OSStatus { return C.csg_delete(service, account) })
	if err != nil && !errors.Is(err, ErrNotFound) {
		return fmt.Errorf("forget the token in the keychain: %w", err)
	}
	return nil
}
