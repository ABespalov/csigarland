// Copyright (c) 2026 Anton Bespalov. All rights reserved.
// Licensed under the terms specified by the MIT License
// Part of the csiGarland project https://github.com/ABespalov/csigarland
//
// Purpose: The Firmware page's check of the public API: every route that reads, after an update or on asking.
// Contributors: None

package ui

import (
	"context"
	"errors"
	"fmt"
	"strings"

	"github.com/ABespalov/csigarland/internal/api"
)

// APICheckText says what a walk of the routes came to: whether every answer reads as this utility
// reads it, which do not and why, and which bring fields from a newer firmware.
func APICheckText(checks []api.RouteCheck) (string, bool) {
	var problems, unknown, absent []string
	for _, check := range checks {
		switch {
		case check.Absent:
			absent = append(absent, check.Route)
		case check.Problem != "":
			problems = append(problems, check.Route+": "+check.Problem)
		case check.Unknown != "":
			unknown = append(unknown, check.Route+" ("+check.Unknown+")")
		}
	}
	if len(problems) > 0 {
		return fmt.Sprintf("The API check found %d of %d routes wrong: %s.", len(problems), len(checks), strings.Join(problems, "; ")), true
	}
	words := fmt.Sprintf("The API answers on all %d routes as this utility reads them.", len(checks)-len(absent))
	if len(absent) > 0 {
		words += " This firmware is older than " + strings.Join(absent, ", ") + ", and says so, as it should."
	}
	if len(unknown) > 0 {
		words += " Some bring fields this utility does not know yet, from a newer firmware: " + strings.Join(unknown, ", ") + "."
	}
	return words, false
}

// checkAPI walks the routes, apart from the page's other work, and says what came of it.
func (p *firmwarePage) checkAPI(a *App) {
	if p.checking {
		return
	}
	p.checking = true
	a.aside(func(ctx context.Context, client *api.Client) (func(), error) {
		checks := client.CheckRoutes(ctx)
		words, failed := APICheckText(checks)
		return func() {
			p.checking = false
			if failed {
				a.reportError(errors.New(words))
			} else {
				a.report(words)
			}
		}, nil
	})
}
