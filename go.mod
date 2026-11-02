module github.com/ABespalov/csigarland

go 1.26.3

// The release builds use this one exactly: deploy/build-utility.ps1 sets GOTOOLCHAIN to it.
toolchain go1.26.4

require (
	gioui.org v0.10.3
	github.com/rs/zerolog v1.35.1
	golang.org/x/image v0.47.0
	golang.org/x/net v0.48.0
	golang.org/x/sys v0.49.0
)

require (
	gioui.org/shader v1.0.9 // indirect
	github.com/go-text/typesetting v0.3.5 // indirect
	github.com/godbus/dbus/v5 v5.2.2 // indirect
	github.com/mattn/go-colorable v0.1.14 // indirect
	github.com/mattn/go-isatty v0.0.20 // indirect
	golang.org/x/exp/shiny v0.0.0-20250408133849-7e4ce0ab07d0 // indirect
	golang.org/x/text v0.43.0 // indirect
)
