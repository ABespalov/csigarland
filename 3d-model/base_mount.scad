/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: The base mount every figurine's cap snaps onto, in two printed parts, so that the
 *          garland is soldered on the bench first and the mounts close round it after: the cup,
 *          which is the flange and the neck the cap takes, open from below, the LED board pushed
 *          up into it against a lip; and the plug, the floor, which snaps in from below and
 *          closes the cable channel. Nothing is threaded and nothing is soldered inside a part.
 * Contributors: Anton Bespalov
 */

include <base_interface.scad>

/* [Display] */
view_mode = "assembled"; // [assembled: Cup and Plug on the LED and Cable, exploded: Exploded Assembly, cutaway: 90-Degree Cross Section, cup_only: Cup (Print Ready), plug_only: Plug (Print Ready), print_set: Cup and Plug Side by Side]
explode_gap = 16;

base_fn = 64;

/* [Cup] */
base_bottom_th    = 6 * layer_h;  // 1.20 mm, the plug's floor: the cup starts above it
base_bottom_r     = 6 * layer_h;  // 1.20 mm, the bottom edge's rounding, all of it on the plug
neck_top_chamfer  = 4 * layer_h;  // 0.80 mm
cable_slot_h      = 10 * layer_h; // 2.00 mm, the channel through the flange, on the plug's floor
cable_slot_w      = 6.00;         // three wires side by side, up to 2.0 mm each (20 AWG silicone)
wire_well_r       = 6.10;         // the wires drop beside the board in two wells, 0.80 mm of neck outside
lip_th            = 3 * layer_h;  // 0.60 mm over the board's rim, at the neck top
lip_d             = 8.30;         // clears the 5050 chip (r 3.54) and the capacitor (r 3.70)

/* [Plug Snap] */
lug_r_in          = 6.50;         // two lugs at +-Y, clear of the cable at +-X
lug_r_out         = 7.40;
lug_w             = 5.00;
lug_h             = 12 * layer_h; // 2.40 mm above the floor
lug_bead          = 0.30;         // the bead on each lug's outer face
lug_gap           = 0.10;         // radial clearance outside a lug: the bead engages lug_bead - lug_gap
lug_flex          = 0.40;         // room inside a lug for it to bend in as it goes home
lug_side_gap      = 0.15;
lug_top_gap       = 0.20;
bead_z0           = 13 * layer_h; // 2.60 mm, the catch face's foot
bead_zp           = 14 * layer_h; // 2.80 mm, the bead's crest
bead_z1           = 3.50;         // the lead-in's top, under the lug's top at 3.60 mm

/* [LED Board] */
led_pcb_arc_d     = 9.50;
led_pcb_flat_w    = 9.05;
led_pcb_th        = 1.25;
led_chip_w        = 5.00;
led_chip_h        = 1.60;
tol_xy            = 0.30;
pocket_arc_d      = led_pcb_arc_d + tol_xy;  // the bore the board rises through
pocket_flat_w     = led_pcb_flat_w + tol_xy; // its flats key the board

neck_top_z        = base_flange_h + neck_h;  // 9.40 mm
lip_z             = neck_top_z - lip_th;     // 8.80 mm, the board's top face
board_z           = lip_z - led_pcb_th;      // 7.55 mm, its back with the pads
wire_z            = base_bottom_th + cable_slot_h / 2;

// Where the board's back and the cable's centre sit, for the pictures that draw them.
function base_board_z() = board_z;
function base_wire_z() = wire_z;

c_base      = [0, 150, 57, 255] / 255; // Bambu PETG Basic Green, #009639
c_pcb       = [0.95, 0.95, 0.95, 1.0];
c_pad_gold  = [0.85, 0.70, 0.25, 1.0];
c_led       = [0.98, 0.98, 0.98, 1.0];
c_lens      = [1.00, 0.90, 0.30, 0.9];
c_wire      = [0.10, 0.45, 0.20, 1.0]; // The cable between figurines is green, all three wires
c_lead      = [0.12, 0.12, 0.12, 1.0]; // The lead from the control unit to the first one is black

// =============================================================================
// The base's outside: the flange, the neck and the snap ridge the cap takes
// =============================================================================
module base_outer() {
    rotate_extrude() {
        hull() {
            translate([base_od/2 - base_bottom_r, base_bottom_r])
                circle(r=base_bottom_r, $fn=32);
            square([base_od/2 - base_bottom_r, base_flange_h]);
            translate([0, base_bottom_r])
                square([base_od/2, base_flange_h - base_bottom_r]);
        }
    }
    translate([0, 0, base_flange_h])
        cylinder(d=neck_od, h=neck_h - neck_top_chamfer);
    translate([0, 0, neck_top_z - neck_top_chamfer])
        cylinder(d1=neck_od, d2=neck_od - 2 * neck_top_chamfer, h=neck_top_chamfer);
    translate([0, 0, base_flange_h + snap_ridge_z])
        cylinder(d1=neck_od, d2=snap_ridge_dia, h=snap_ridge_h * 0.4);
    translate([0, 0, base_flange_h + snap_ridge_z + snap_ridge_h * 0.4])
        cylinder(d1=snap_ridge_dia, d2=neck_od, h=snap_ridge_h * 0.6);
}

// The board's outline, a circle with two flats at +-Y, h tall.
module keyed_disc(d, w, h) {
    intersection() {
        cylinder(d=d, h=h);
        translate([-d/2 - 1, -w/2, 0]) cube([d + 2, w, h]);
    }
}

module ring(r0, r1, h) {
    difference() {
        cylinder(r=r1, h=h);
        translate([0, 0, -1]) cylinder(r=r0, h=h + 2);
    }
}

// The half-space on side s of the XZ plane, w wide along X.
module lug_slab(s, w) {
    translate([-w/2, s > 0 ? 0 : -20, -1]) cube([w, 20, 30]);
}

// =============================================================================
// The cup: everything above the floor, open from below
// =============================================================================
// The board goes up the keyed bore and stops under the lip, its pads facing down. Its wires
// leave the pads at +-X, drop in the two wells beside the bore and turn out along the channel
// to the cable, which lies on the plug. The cup prints standing on its open bottom with no
// support: the channel and the wells end under short bridges, the lip overhangs 0.75 mm.
module base_cup() {
    $fn = base_fn;
    color(c_base)
    difference() {
        intersection() {
            base_outer();
            translate([-20, -20, base_bottom_th]) cube([40, 40, 20]);
        }
        translate([0, 0, base_bottom_th - 0.1])
            keyed_disc(pocket_arc_d, pocket_flat_w, lip_z - base_bottom_th + 0.1);
        translate([0, 0, lip_z - 0.1])
            cylinder(d=lip_d, h=lip_th + 0.2);
        translate([0, 0, base_bottom_th - 0.1])
            intersection() {
                cylinder(r=wire_well_r, h=board_z - base_bottom_th + 0.1);
                translate([-wire_well_r, -cable_slot_w/2, 0])
                    cube([2 * wire_well_r, cable_slot_w, board_z]);
            }
        translate([-base_od, -cable_slot_w/2, base_bottom_th - 0.1])
            cube([base_od * 2, cable_slot_w, cable_slot_h + 0.1]);
        for (s = [-1, 1]) lug_socket(s);
    }
}

// The pocket a lug goes up into, with the groove its bead clicks into.
module lug_socket(s) {
    intersection() {
        union() {
            translate([0, 0, base_bottom_th - 0.1])
                ring(lug_r_in - lug_flex, lug_r_out + lug_gap, lug_h + lug_top_gap + 0.1);
            translate([0, 0, bead_z0 - 0.05])
                ring(lug_r_in, lug_r_out + lug_gap + lug_bead + 0.05, bead_z1 - bead_z0 + 0.1);
        }
        lug_slab(s, lug_w + 2 * lug_side_gap);
    }
}

// =============================================================================
// The plug: the floor, with two lugs that snap into the cup
// =============================================================================
// It closes the cable channel from below; the cable lies on it. Prints floor down.
module base_plug() {
    $fn = base_fn;
    color(c_base) {
        intersection() {
            base_outer();
            translate([-20, -20, -1]) cube([40, 40, 1 + base_bottom_th]);
        }
        for (s = [-1, 1])
            intersection() {
                union() {
                    translate([0, 0, base_bottom_th - 0.01])
                        ring(lug_r_in, lug_r_out, lug_h + 0.01);
                    rotate_extrude()
                        polygon([[lug_r_out - 0.01, bead_z0], [lug_r_out + lug_bead, bead_zp],
                                 [lug_r_out - 0.01, bead_z1]]);
                }
                lug_slab(s, lug_w);
            }
    }
}

// =============================================================================
// Dummies: the WS2812B breakout and the cable
// =============================================================================
module led_pcb_dummy() {
    color(c_pcb)
        keyed_disc(led_pcb_arc_d, led_pcb_flat_w, led_pcb_th);

    color(c_pad_gold) {
        for (side = [-1, 1]) {
            for (py = [-2.2, 0, 2.2]) {
                translate([side * 3.3, py, -0.05])
                    cube([1.8, 1.4, 0.1], center=true);
            }
        }
    }

    color(c_led)
        translate([0, 0, led_pcb_th + led_chip_h / 2])
            cube([led_chip_w, led_chip_w, led_chip_h], center=true);

    color(c_lens)
        translate([0, 0, led_pcb_th + led_chip_h])
            cylinder(d=3.2, h=0.2);

    color([0.65, 0.55, 0.45])
        translate([0, 3.2, led_pcb_th + 0.45])
            cube([1.6, 0.8, 0.9], center=true);
}

wire_pitch = 2.00;
wire_d     = 2.00;

module wire_harness_dummy(len=36, lead=false) {
    color(lead ? c_lead : c_wire)
        for (py = [-wire_pitch, 0, wire_pitch])
            translate([0, py, 0])
                rotate([0, 90, 0])
                    cylinder(d=wire_d, h=len, center=true, $fn=16);
}

// The wires from the pads down the wells to the channel, as wide as the channel allows.
module wire_risers() {
    color(c_wire)
        for (side = [-1, 1]) for (py = [-wire_pitch, 0, wire_pitch])
            translate([side * 4.6, py, wire_z])
                cylinder(d=wire_d, h=board_z - wire_z, $fn=16);
}

// The base as it goes together: the board on its wires, the cup lowered over it from
// above and the plug pushed in from below. gap = 0 is the base assembled.
module base_assembly(gap = 0, wires = true) {
    translate([0, 0, -gap * 0.35]) base_plug();
    if (wires)
        translate([0, 0, wire_z]) wire_harness_dummy(base_od + 14);
    wire_risers();
    translate([0, 0, board_z]) led_pcb_dummy();
    translate([0, 0, gap * 0.9]) base_cup();
}

if (view_mode == "assembled") {
    base_assembly();
} else if (view_mode == "exploded") {
    base_assembly(explode_gap);
} else if (view_mode == "cutaway") {
    difference() {
        base_assembly();
        translate([0, 0, -1]) cube([25, 25, 20]);
    }
} else if (view_mode == "cup_only") {
    translate([0, 0, -base_bottom_th]) base_cup();
} else if (view_mode == "plug_only") {
    base_plug();
} else if (view_mode == "print_set") {
    translate([-11, 0, -base_bottom_th]) base_cup();
    translate([11, 0, 0]) base_plug();
}
