/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric Cozy Winter Snowman Diffuser Cap & Universal Base Mount (50mm Standard)
 *          Fully optimized for FDM 3D printing with 0.40mm nozzle and 0.20mm layers.
 * Contributors: Anton Bespalov
 */

/* [Display & Animation Options] */
view_mode = "assembled"; // [exploded: Exploded Assembly View, assembled: Full Assembled Figurine, cutaway: 90-Degree Cross Section, cap_only: Diffuser Cap (Print Ready), base_only: Base Mount (Print Ready)]
explode_gap = 24;

$fn = 40;

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
snowman_height    = 238 * layer_h;// 47.60 mm

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

c_cap       = [0.96, 0.98, 1.00, 0.88]; // Snow-white / crystal translucent PETG
c_base      = [0.15, 0.45, 0.25, 1.0];
c_pcb       = [0.95, 0.95, 0.95, 1.0];
c_pad_gold  = [0.85, 0.70, 0.25, 1.0];
c_led       = [0.98, 0.98, 0.98, 1.0];
c_lens      = [1.00, 0.90, 0.30, 0.9];
c_wire_vcc  = [0.85, 0.15, 0.15, 1.0];
c_wire_dat  = [0.15, 0.75, 0.20, 1.0];
c_wire_gnd  = [0.20, 0.20, 0.25, 1.0];

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

// =============================================================================
// SUB-MODULE: Cozy Snowman Diffuser Cap (Print Ready, Sealed Plafond)
// =============================================================================
module snowman_diffuser_cap() {
    z_head = 19.5;
    r_head = 8.2;
    z_body = 35.0;
    r_body = 11.2;

    color(c_cap)
    difference() {
        union() {
            // 1. Lower collar with rounded upper edge + beanie hat (Z = 0..15.5 mm)
            rotate_extrude($fn=48) {
                polygon([
                    [0, 0],
                    [base_od/2, 0],
                    [base_od/2, 4.4],       // full 17.6mm OD protecting snap groove (wall = 1.425mm)
                    [base_od/2 - 0.2, 5.2], // smooth rounded fillet R=2.0mm
                    [base_od/2 - 0.8, 6.0],
                    [base_od/2 - 1.4, 6.8],
                    [8.2, 15.5],
                    [0, 15.5]
                ]);
            }

            // Beanie folded brim roll
            translate([0, 0, 15.5])
                rotate_extrude($fn=36)
                    translate([8.2, 0])
                        circle(r=0.9, $fn=16);

            // 2. Head sphere (Z = 19.5 mm)
            translate([0, 0, z_head])
                sphere(r=r_head, $fn=36);

            // Coal eyes facing front (-Y)
            translate([-2.6, -7.5, z_head - 1.6])
                sphere(r=1.1, $fn=16);
            translate([2.6, -7.5, z_head - 1.6])
                sphere(r=1.1, $fn=16);

            // Carrot nose pointing FORWARD (towards -Y, into the room!)
            translate([0, -7.6, z_head + 0.2])
                rotate([90, 0, 0])
                    cylinder(r1=1.6, r2=0.35, h=5.8, $fn=16);

            // Cheerful happy smile arc (corners curl up towards eyes when hanging!)
            for (ang = [-38, -19, 0, 19, 38]) {
                translate([3.0 * sin(ang), -7.4 * cos(ang * 0.5), z_head + 2.1 + 0.8 * cos(ang * 1.8)])
                    sphere(r=0.6, $fn=10);
            }

            // 3. Thick sculpted holiday scarf wrapped around neck (Z = 24.5..28.5 mm)
            // Primary warm wrap
            translate([0, 0, 25.5])
                rotate_extrude($fn=36)
                    translate([7.0, 0])
                        scale([1.1, 1.2])
                            circle(r=1.9, $fn=16);

            // Secondary wrap fold
            translate([0, 0, 26.8])
                rotate_extrude($fn=36)
                    translate([7.4, 0])
                        scale([1.1, 0.9])
                            circle(r=1.6, $fn=16);

            // Scarf knot in front-right (-Y, +X)
            translate([2.4, -7.5, 26.5])
                scale([1.2, 0.85, 1.1])
                    sphere(r=2.2, $fn=16);

            // Scarf tail 1 (long ribbon draped flat across chest)
            hull() {
                translate([1.8, -7.4, 26.5])
                    scale([1.4, 0.45, 1.0]) sphere(r=1.8, $fn=14);
                translate([2.8, -10.0, 34.0])
                    scale([1.4, 0.45, 1.0]) sphere(r=1.6, $fn=14);
            }
            // Tail 1 fringe end
            translate([2.8, -10.1, 34.5])
                scale([1.5, 0.35, 0.6]) sphere(r=1.6, $fn=14);

            // Scarf tail 2 (shorter ribbon beside it)
            hull() {
                translate([3.6, -7.0, 26.8])
                    scale([1.3, 0.45, 1.0]) sphere(r=1.6, $fn=14);
                translate([4.6, -9.2, 31.8])
                    scale([1.3, 0.45, 1.0]) sphere(r=1.5, $fn=14);
            }
            // Tail 2 fringe end
            translate([4.6, -9.3, 32.3])
                scale([1.4, 0.35, 0.6]) sphere(r=1.5, $fn=14);

            // 4. Plump round lower snowball body (Z = 35.0 mm)
            translate([0, 0, z_body])
                scale([1, 1, 0.96])
                    sphere(r=r_body, $fn=36);

            // Coal buttons on belly (front facing -Y, placed beside the scarf)
            for (bz = [32.5, 37.0]) {
                translate([-1.2, -r_body + 0.7, bz])
                    sphere(r=1.1, $fn=14);
            }

            // Ball hands on the sides (welcoming friendly posture)
            // Left hand & arm
            hull() {
                translate([-8.5, 0, z_body - 2.4]) sphere(r=2.6, $fn=16);
                translate([-12.0, -2.8, z_body - 1.2]) sphere(r=3.2, $fn=20);
            }
            // Right hand & arm
            hull() {
                translate([8.5, 0, z_body - 2.4]) sphere(r=2.6, $fn=16);
                translate([12.0, -2.8, z_body - 1.2]) sphere(r=3.2, $fn=20);
            }

            // Bottom base rounding cap (Z = 45.0..48.0 mm)
            translate([0, 0, z_body + r_body * 0.96 - 0.6])
                sphere(r=2.5, $fn=20);
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
            cylinder(r1=neck_od/2 - wall_thickness, r2=5.6, h=4.0);
        translate([0, 0, neck_h + 3.9])
            cylinder(r1=5.6, r2=6.0, h=8.5);
        
        // Head cavity (wall >= 1.4mm)
        translate([0, 0, z_head])
            sphere(r=r_head - wall_thickness * 1.3, $fn=24);

        // Neck cavity
        translate([0, 0, 25.2])
            cylinder(r1=5.8, r2=6.2, h=3.2);

        // Lower body cavity
        translate([0, 0, z_body])
            scale([1, 1, 0.96])
                sphere(r=r_body - wall_thickness * 1.3, $fn=24);
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
        snowman_diffuser_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=snowman_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_mount();
    translate([0, 0, base_bottom_th + cable_slot_h / 2])
        wire_harness_dummy(base_od + 14);
    translate([0, 0, base_flange_h + neck_h - pocket_depth])
        led_pcb_dummy();
    translate([0, 0, base_flange_h])
        snowman_diffuser_cap();
}

module cutaway_view() {
    difference() {
        assembled_view();
        translate([0, 0, -1])
            cube([25, 25, snowman_height + 15]);
    }
}

if (view_mode == "exploded") {
    exploded_assembly();
} else if (view_mode == "assembled") {
    assembled_view();
} else if (view_mode == "cutaway") {
    cutaway_view();
} else if (view_mode == "cap_only") {
    snowman_diffuser_cap();
} else if (view_mode == "base_only") {
    base_mount();
}
