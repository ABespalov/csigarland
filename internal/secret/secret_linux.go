// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The token store on Linux: the Secret Service, as GNOME Keyring and KWallet give it.
// Contributors: None

package secret

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"sync/atomic"
	"time"

	"github.com/godbus/dbus/v5"
)

const (
	serviceName      = "org.freedesktop.secrets"
	servicePath      = dbus.ObjectPath("/org/freedesktop/secrets")
	serviceInterface = "org.freedesktop.Secret.Service"
	itemInterface    = "org.freedesktop.Secret.Item"
	promptInterface  = "org.freedesktop.Secret.Prompt"
	noPath           = dbus.ObjectPath("/")
	// An item is found by these, the target as on Windows.
	attributeApp    = "application"
	attributeTarget = "target"
	appName         = "csiGarland"
	// A call answers at once, or the service is not there.
	callTimeout = 10 * time.Second
	// An unlock waits on the owner, who types the keyring's password.
	promptTimeout = 2 * time.Minute
)

var (
	// One call of the store at a time: two at once could each ask the owner for the keyring's password.
	storeLock sync.Mutex
	// A prompt the owner dismissed is not shown again until the utility starts again: each garland's token
	// asked for in turn would otherwise ask once more.
	dismissed atomic.Bool
)

// errDismissed says the owner dismissed the keyring's prompt, now or before.
var errDismissed = errors.New("the keyring's prompt was dismissed")

// As the Secret Service's specification has it: (oayays).
type secretValue struct {
	Session     dbus.ObjectPath
	Parameters  []byte
	Value       []byte
	ContentType string
}

// store is one connection to the session bus, with a session of the service on it.
type store struct {
	conn    *dbus.Conn
	session dbus.ObjectPath
}

// open connects. A system with no session bus, or no service on it, has no store: ErrUnsupported.
//
// The session is plain: the token crosses the session bus, which only this user reaches, unencrypted,
// as anyone who could read it there could as well ask the service for it.
func open() (*store, error) {
	conn, err := dbus.ConnectSessionBus()
	if err != nil {
		return nil, fmt.Errorf("%w: no session bus: %w", ErrUnsupported, err)
	}
	s := &store{conn: conn}
	var output dbus.Variant
	if err := s.call(servicePath, serviceInterface+".OpenSession", "plain", dbus.MakeVariant("")).Store(&output, &s.session); err != nil {
		_ = conn.Close()
		var dbusErr dbus.Error
		if errors.As(err, &dbusErr) && dbusErr.Name == "org.freedesktop.DBus.Error.ServiceUnknown" {
			return nil, fmt.Errorf("%w: no Secret Service on the session bus", ErrUnsupported)
		}
		return nil, fmt.Errorf("open a session of the Secret Service: %w", err)
	}
	return s, nil
}

// call waits for one method's answer, as long as a service takes to answer at once.
func (s *store) call(path dbus.ObjectPath, method string, args ...any) *dbus.Call {
	ctx, cancel := context.WithTimeout(context.Background(), callTimeout)
	defer cancel()
	return s.conn.Object(serviceName, path).CallWithContext(ctx, method, 0, args...)
}

func (s *store) close() {
	_ = s.call(s.session, "org.freedesktop.Secret.Session.Close").Err
	_ = s.conn.Close()
}

func attributes(name string) map[string]string {
	return map[string]string{attributeApp: appName, attributeTarget: targetPrefix + name}
}

// prompt shows the service's prompt, if it gave one, and waits for the owner. A prompt dismissed is
// a failure: what it was for did not happen.
func (s *store) prompt(prompt dbus.ObjectPath) error {
	if prompt == noPath || prompt == "" {
		return nil
	}
	if dismissed.Load() {
		return errDismissed
	}
	ctx, cancel := context.WithTimeout(context.Background(), promptTimeout)
	defer cancel()
	if err := s.conn.AddMatchSignalContext(ctx, dbus.WithMatchObjectPath(prompt), dbus.WithMatchInterface(promptInterface),
		dbus.WithMatchMember("Completed")); err != nil {
		return fmt.Errorf("wait for the keyring's prompt: %w", err)
	}
	signals := make(chan *dbus.Signal, 4)
	s.conn.Signal(signals)
	defer s.conn.RemoveSignal(signals)
	// No window of ours to name: the prompt opens over whatever is in front.
	if err := s.call(prompt, promptInterface+".Prompt", "").Err; err != nil {
		return fmt.Errorf("show the keyring's prompt: %w", err)
	}
	for {
		select {
		case <-ctx.Done():
			return errors.New("the keyring's prompt was not answered in time")
		case signal, open := <-signals:
			if !open {
				return errors.New("the session bus went away during the keyring's prompt")
			}
			if signal.Path != prompt || signal.Name != promptInterface+".Completed" || len(signal.Body) == 0 {
				continue
			}
			if wasDismissed, _ := signal.Body[0].(bool); wasDismissed {
				dismissed.Store(true)
				return errDismissed
			}
			return nil
		}
	}
}

// unlock opens what is locked, asking the owner if the service wants to.
func (s *store) unlock(paths []dbus.ObjectPath) error {
	if len(paths) == 0 {
		return nil
	}
	var unlocked []dbus.ObjectPath
	var prompt dbus.ObjectPath
	if err := s.call(servicePath, serviceInterface+".Unlock", paths).
		Store(&unlocked, &prompt); err != nil {
		return fmt.Errorf("unlock the keyring: %w", err)
	}
	return s.prompt(prompt)
}

// find is every item kept under a name, unlocked.
func (s *store) find(name string) ([]dbus.ObjectPath, error) {
	var unlocked, locked []dbus.ObjectPath
	if err := s.call(servicePath, serviceInterface+".SearchItems",
		attributes(name)).Store(&unlocked, &locked); err != nil {
		return nil, fmt.Errorf("look in the keyring: %w", err)
	}
	if err := s.unlock(locked); err != nil {
		return nil, err
	}
	return append(unlocked, locked...), nil
}

// defaultCollection is where the owner's own secrets go, unlocked.
func (s *store) defaultCollection() (dbus.ObjectPath, error) {
	var collection dbus.ObjectPath
	if err := s.call(servicePath, serviceInterface+".ReadAlias", "default").
		Store(&collection); err != nil {
		return "", fmt.Errorf("find the default keyring: %w", err)
	}
	if collection == noPath {
		return "", errors.New("the system has no default keyring")
	}
	if err := s.unlock([]dbus.ObjectPath{collection}); err != nil {
		return "", err
	}
	return collection, nil
}

func withStore[T any](do func(*store) (T, error)) (T, error) {
	storeLock.Lock()
	defer storeLock.Unlock()
	s, err := open()
	if err != nil {
		var none T
		return none, err
	}
	defer s.close()
	return do(s)
}

// Set keeps a token under a name, replacing what was there.
func Set(name, token string) error {
	if token == "" {
		return errors.New("no token to keep")
	}
	_, err := withStore(func(s *store) (struct{}, error) {
		collection, err := s.defaultCollection()
		if err != nil {
			return struct{}{}, err
		}
		properties := map[string]dbus.Variant{
			itemInterface + ".Label":      dbus.MakeVariant(targetPrefix + name),
			itemInterface + ".Attributes": dbus.MakeVariant(attributes(name)),
		}
		value := secretValue{Session: s.session, Parameters: []byte{}, Value: []byte(token), ContentType: "text/plain"}
		var item, prompt dbus.ObjectPath
		if err := s.call(collection, "org.freedesktop.Secret.Collection.CreateItem",
			properties, value, true).Store(&item, &prompt); err != nil {
			return struct{}{}, fmt.Errorf("keep the token: %w", err)
		}
		if err := s.prompt(prompt); err != nil {
			return struct{}{}, fmt.Errorf("keep the token: %w", err)
		}
		return struct{}{}, nil
	})
	return err
}

// Get reads the token kept under a name, or ErrNotFound. A system with no store has none: ErrNotFound.
func Get(name string) (string, error) {
	token, err := withStore(func(s *store) (string, error) {
		items, err := s.find(name)
		if err != nil {
			return "", err
		}
		if len(items) == 0 {
			return "", ErrNotFound
		}
		var value secretValue
		if err := s.call(items[0], itemInterface+".GetSecret", s.session).
			Store(&value); err != nil {
			return "", fmt.Errorf("read the token: %w", err)
		}
		return string(value.Value), nil
	})
	if errors.Is(err, ErrUnsupported) {
		return "", ErrNotFound
	}
	return token, err
}

// Delete forgets the token under a name. One not there, or no store at all, is no failure.
func Delete(name string) error {
	_, err := withStore(func(s *store) (struct{}, error) {
		items, err := s.find(name)
		if err != nil {
			return struct{}{}, err
		}
		for _, item := range items {
			var prompt dbus.ObjectPath
			if err := s.call(item, itemInterface+".Delete").Store(&prompt); err != nil {
				return struct{}{}, fmt.Errorf("forget the token: %w", err)
			}
			if err := s.prompt(prompt); err != nil {
				return struct{}{}, fmt.Errorf("forget the token: %w", err)
			}
		}
		return struct{}{}, nil
	})
	if errors.Is(err, ErrUnsupported) {
		return nil
	}
	return err
}
