// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The csiGarland utility: its window, and its version on the command line.
// Contributors: Anton Bespalov

package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"

	"github.com/ABespalov/csigarland/internal/ui"
)

func main() {
	showVersion := flag.Bool("version", false, "print the version and stop")
	flag.Parse()
	if *showVersion {
		fmt.Fprintln(os.Stdout, "csiGarland", Version)
		return
	}
	log.Logger = log.Output(zerolog.ConsoleWriter{Out: os.Stderr})
	log.Info().Str("version", Version).Msg("csiGarland utility starting")
	ui.Run(Version, os.Exit)
}
