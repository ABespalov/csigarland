/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric Vintage Holiday Lantern Diffuser Cap & Universal Base Mount (50mm Standard)
 *          Fully optimized for FDM 3D printing with 0.40mm nozzle and 0.20mm layers.
 * Contributors: Anton Bespalov
 */

/* [Display & Animation Options] */
view_mode = "assembled"; // [exploded: Exploded Assembly View, assembled: Full Assembled Figurine, cutaway: 90-Degree Cross Section, cap_only: Diffuser Cap (Print Ready), base_only: Base Mount (Print Ready)]
explode_gap = 24;

$fn = 36;

/* [FDM Nozzle 0.4mm & Layer 0.20mm Slicing Parameters] */
layer_h         = 0.20;
nozzle_w        = 0.40;
line_w_outer    = 0.42;
line_w_inner    = 0.44;
wall_thickness  = 1.40; // 3-4 solid perimeters (minimum 1.40 mm everywhere)

/* [Base Mount Dimensions] */
base_bottom_th    = 6 * layer_h;  // 1.20 mm
base_flange_h     = 22 * layer_h; // 4.40 mm
neck_h            = 25 * layer_h; // 5.00 mm
pocket_depth      = 7 * layer_h;  // 1.40 mm
cable_slot_h      = 10 * layer_h; // 2.00 mm
snap_ridge_z      = 11 * layer_h; // 2.20 mm
snap_ridge_h      = 6 * layer_h;  // 1.20 mm
neck_top_chamfer  = 4 * layer_h;  // 0.80 mm
base_bottom_r     = 6 * layer_h;  // 1.20 mm
lantern_height    = 237.5 * layer_h; // 47.50 mm

base_od           = 17.60;
neck_od           = 13.80;
neck_socket_gap   = 0.20;
socket_axial_gap  = 0.30; // neck top clears the socket ceiling (bridge sag, seating)
snap_ridge_dia    = 14.40;
cable_slot_w      = 6.00; // three 20 AWG silicone wires side by side
solder_zone_w     = 6.00;

led_pcb_arc_d     = 9.50;
led_pcb_flat_w    = 9.05;
led_pcb_th        = 1.25;
led_chip_w        = 5.00;
led_chip_h        = 1.60;

tol_xy            = 0.30;
pocket_arc_d      = led_pcb_arc_d + tol_xy;
pocket_flat_w     = led_pcb_flat_w + tol_xy;

c_cap       = [0.52, 0.54, 0.58, 0.85]; // Bambu PETG Translucent Gray
c_base      = [0.15, 0.45, 0.25, 1.0];
c_pcb       = [0.95, 0.95, 0.95, 1.0];
c_pad_gold  = [0.85, 0.70, 0.25, 1.0];
c_led       = [0.98, 0.98, 0.98, 1.0];
c_lens      = [1.00, 0.90, 0.30, 0.9];
c_wire_vcc  = [0.18, 0.55, 0.28, 1.0]; // The cable is green, all three wires
c_wire_dat  = [0.18, 0.55, 0.28, 1.0]; // The cable is green, all three wires
c_wire_gnd  = [0.18, 0.55, 0.28, 1.0]; // The cable is green, all three wires

// =============================================================================
// SUB-MODULE: Base Mount
// =============================================================================
module base_mount() {
    color(c_base)
    difference() {
        union() {
            rotate_extrude($fn=$fn) {
                hull() {
                    translate([base_od/2 - base_bottom_r, base_bottom_r])
                        circle(r=base_bottom_r, $fn=32);
                    translate([0, 0])
                        square([base_od/2 - base_bottom_r, base_flange_h]);
                    translate([0, base_bottom_r])
                        square([base_od/2, base_flange_h - base_bottom_r]);
                }
            }
            translate([0, 0, base_flange_h])
                cylinder(d=neck_od, h=neck_h - neck_top_chamfer);
            translate([0, 0, base_flange_h + neck_h - neck_top_chamfer])
                cylinder(d1=neck_od, d2=neck_od - 2 * neck_top_chamfer, h=neck_top_chamfer);
            translate([0, 0, base_flange_h + snap_ridge_z])
                cylinder(d1=neck_od, d2=snap_ridge_dia, h=snap_ridge_h * 0.4);
            translate([0, 0, base_flange_h + snap_ridge_z + snap_ridge_h * 0.4])
                cylinder(d1=snap_ridge_dia, d2=neck_od, h=snap_ridge_h * 0.6);
        }

        translate([0, 0, base_flange_h + neck_h - pocket_depth])
            intersection() {
                cylinder(d=pocket_arc_d, h=pocket_depth + 0.1);
                cube([pocket_arc_d + 1.0, pocket_flat_w, (pocket_depth + 0.1) * 2], center=true);
            }

        translate([0, 0, base_bottom_th])
            intersection() {
                cylinder(d=pocket_arc_d, h=base_flange_h + neck_h - base_bottom_th + 0.1);
                translate([0, 0, (base_flange_h + neck_h) / 2])
                    cube([pocket_arc_d + 0.2, solder_zone_w, base_flange_h + neck_h + 1], center=true);
            }

        translate([0, 0, base_bottom_th])
            cylinder(d=5.5, h=base_flange_h + neck_h - base_bottom_th + 0.1);

        translate([-base_od, -cable_slot_w/2, base_bottom_th])
            cube([base_od * 2, cable_slot_w, cable_slot_h]);
    }
}

// =============================================================================
// SUB-MODULE: WS2812B LED Breakout Dummy
// =============================================================================
module led_pcb_dummy() {
    color(c_pcb)
        intersection() {
            cylinder(d=led_pcb_arc_d, h=led_pcb_th, center=false);
            translate([0, 0, led_pcb_th / 2])
                cube([led_pcb_arc_d + 1.0, led_pcb_flat_w, led_pcb_th + 0.1], center=true);
        }

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

// =============================================================================
// SUB-MODULE: 3-Wire Cable Harness Dummy
// =============================================================================
module wire_harness_dummy(len=36) {
    wire_pitch = 2.00;
    wire_d = 2.00;

    color(c_wire_vcc)
        translate([0, -wire_pitch, 0])
            rotate([0, 90, 0])
                cylinder(d=wire_d, h=len, center=true);

    color(c_wire_dat)
        translate([0, 0, 0])
            rotate([0, 90, 0])
                cylinder(d=wire_d, h=len, center=true);

    color(c_wire_gnd)
        translate([0, wire_pitch, 0])
            rotate([0, 90, 0])
                cylinder(d=wire_d, h=len, center=true);
}

module lantern_window_bas_relief() {
    // Hanging Orientation: Arch is at low Z (Z=1.4 mm = TOP when hanging!)
    // Flat sill is at high Z (Z=10.8 mm = BOTTOM when hanging!)
    for (sy = [-2.6, 2.6]) {
        translate([0, sy, 5.8])
            cube([2.0, 0.9, 9.8], center=true);
    }
    
    // Arched top header
    difference() {
        translate([0, 0, 1.4])
            rotate([0, 90, 0])
                cylinder(d=5.2, h=2.0, center=true, $fn=24);
        translate([0, 0, 1.4])
            rotate([0, 90, 0])
                cylinder(d=3.4, h=2.5, center=true, $fn=24);
        translate([0, 0, -2.0])
            cube([3.0, 6.0, 6.0], center=true);
    }

    // Bottom sill
    translate([0, 0, 10.8])
        cube([2.0, 5.8, 1.1], center=true);

    // Vertical mullion
    translate([0, 0, 5.8])
        cube([1.8, 0.8, 9.0], center=true);
    // Horizontal transom bar
    translate([0, 0, 5.8])
        cube([1.8, 4.8, 0.8], center=true);

    // Embossed diamond star
    translate([0.3, 0, 5.8])
        rotate([45, 0, 0])
            cube([1.6, 2.4, 2.4], center=true);
}

// =============================================================================
// SUB-MODULE: Vintage Lantern Diffuser Cap (Print Ready, Sealed Plafond)
// =============================================================================
module lantern_diffuser_cap() {
    color(c_cap)
    difference() {
        union() {
            // Lower collar matching base mount flange (Z = 0..4.4 mm)
            cylinder(d=base_od, h=4.4);

            // Chamfer tapering collar inward from d=17.6 to d=15.2 at Z=5.2 mm (eliminates flat steps!)
            translate([0, 0, 4.39])
                cylinder(d1=base_od, d2=15.2, h=0.81);

            // Pagoda canopy roof starting from d=17.6 (flat-to-flat = 15.24 >= 15.2 mm chamfer top),
            // so the roof flares at ~33 deg from vertical instead of a flat 1.5-2.9 mm overhang ring
            translate([0, 0, 5.2])
                cylinder(d1=17.6, d2=26.4, h=6.8, $fn=6);

            // Eaves cornice rim (Z = 12.0 to 13.5 mm)
            translate([0, 0, 12.0])
                cylinder(d=27.2, h=1.5, $fn=6);

            // 2. Hexagonal Lantern Glass Cage & Corner Pillars (Z = 13.5 to 32.5 mm)
            // 6 Corner fluted columns
            for (a = [0:60:300]) {
                rotate([0, 0, a])
                    translate([11.8, 0, 13.5])
                        cylinder(d=3.2, h=19.0, $fn=16);
            }

            // Faceted 6-sided crystal window body
            translate([0, 0, 13.5])
                cylinder(d1=22.4, d2=19.6, h=19.0, $fn=6);

            // Prominent bas-relief on each of the 6 flat window panes
            for (a = [30:60:330]) {
                rotate([0, 0, a])
                    translate([8.8, 0, 15.5])
                        rotate([0, 4.2, 0])
                            lantern_window_bas_relief();
            }

            // 3. Bottom Base Tray & Cornice (Z = 32.5 to 38.5 mm)
            translate([0, 0, 32.5])
                cylinder(d1=21.4, d2=16.8, h=3.8, $fn=6);
            translate([0, 0, 36.3])
                cylinder(d1=16.8, d2=11.0, h=2.5, $fn=6);

            // 4. Decorative Droplet Finial / Acorn drop at bottom (Z = 38.5 to 48.0 mm)
            translate([0, 0, 38.5]) {
                cylinder(d1=9.6, d2=5.0, h=2.8, $fn=16);
                translate([0, 0, 4.3])
                    scale([1, 1, 1.4])
                        sphere(r=3.4, $fn=24);
            }
        }

        // 1. Socket cavity for base neck with entrance chamfer
        translate([0, 0, -0.1])
            cylinder(d=neck_od + neck_socket_gap, h=neck_h + socket_axial_gap + 0.1);
        translate([0, 0, -0.1])
            cylinder(d1=neck_od + neck_socket_gap + 1.2, d2=neck_od + neck_socket_gap, h=0.8);

        // 2. Internal snap-fit groove
        translate([0, 0, snap_ridge_z - 0.1])
            cylinder(d1=neck_od + neck_socket_gap, d2=snap_ridge_dia + 0.35, h=snap_ridge_h * 0.5);
        translate([0, 0, snap_ridge_z + snap_ridge_h * 0.5 - 0.1])
            cylinder(d1=snap_ridge_dia + 0.35, d2=neck_od + neck_socket_gap, h=snap_ridge_h * 0.5);

        // 3. Hollow light-diffusion chamber inside
        translate([0, 0, neck_h])
            cylinder(r1=neck_od/2 - wall_thickness, r2=8.6, h=7.0);
        translate([0, 0, neck_h + 6.9])
            cylinder(r1=8.6, r2=7.6, h=19.5, $fn=6);
        translate([0, 0, neck_h + 26.3])
            cylinder(r1=7.6, r2=2.0, h=7.5, $fn=6);
    }
}

// =============================================================================
// MAIN ASSEMBLY CONTROLLER
// =============================================================================
module exploded_assembly() {
    base_mount();
    translate([0, 0, base_bottom_th + cable_slot_h / 2])
        wire_harness_dummy(base_od + 14);
    translate([0, 0, base_flange_h + neck_h - pocket_depth + explode_gap * 0.45])
        led_pcb_dummy();
    translate([0, 0, base_flange_h + explode_gap * 1.25])
        lantern_diffuser_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=lantern_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_mount();
    translate([0, 0, base_bottom_th + cable_slot_h / 2])
        wire_harness_dummy(base_od + 14);
    translate([0, 0, base_flange_h + neck_h - pocket_depth])
        led_pcb_dummy();
    translate([0, 0, base_flange_h])
        lantern_diffuser_cap();
}

module cutaway_view() {
    difference() {
        assembled_view();
        translate([0, 0, -1])
            cube([25, 25, lantern_height + 15]);
    }
}

if (view_mode == "exploded") {
    exploded_assembly();
} else if (view_mode == "assembled") {
    assembled_view();
} else if (view_mode == "cutaway") {
    cutaway_view();
} else if (view_mode == "cap_only") {
    lantern_diffuser_cap();
} else if (view_mode == "base_only") {
    base_mount();
}
