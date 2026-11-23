/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The fit between a figurine's cap and its base mount: the flange the collar
 *          seats on, the neck the socket takes and the snap ridge, in one place for the
 *          base (base_mount.scad) and the eight caps (figurine_*.scad), which include it.
 *          Every cap is printed to these numbers: change one and the caps change too.
 * Contributors: Anton Bespalov
 */

layer_h           = 0.20;         // every height of the base is whole layers of 0.20 mm

base_od           = 17.60;        // the flange, flush with the cap's collar
base_flange_h     = 22 * layer_h; // 4.40 mm, its top face is the collar's seat
neck_od           = 13.80;
neck_h            = 25 * layer_h; // 5.00 mm above the flange
snap_ridge_z      = 11 * layer_h; // 2.20 mm, the ridge's foot above the flange
snap_ridge_h      = 6 * layer_h;  // 1.20 mm
snap_ridge_dia    = 14.40;

neck_socket_gap   = 0.20;         // the socket's bore is neck_od + this
socket_axial_gap  = 0.30;         // neck top clears the socket ceiling (bridge sag, seating)
