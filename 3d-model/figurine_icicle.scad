/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric Natural Icicle Cluster Diffuser Cap & Universal Base Mount (50mm Standard)
 *          Fully optimized for FDM 3D printing with 0.40mm nozzle and 0.20mm layers.
 * Contributors: Anton Bespalov
 */

/* [Display & Animation Options] */
view_mode = "assembled"; // [exploded: Exploded Assembly View, assembled: Full Assembled Figurine, cutaway: 90-Degree Cross Section, cap_only: Diffuser Cap (Print Ready), base_only: Base Mount (Print Ready)]
explode_gap = 24;

$fn = 48;

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
cluster_height    = 240 * layer_h;// 48.00 mm

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

c_cap       = [0.82, 0.94, 1.00, 0.85]; // Icy translucent cyan PETG
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
// SUB-MODULE: Natural Wavy Stalactite / Icicle Generator
// =============================================================================
module single_icicle(h=32, r_base=4.8, r_tip=0.8, curve_amp=1.8, curve_dir=0, ripple_amp=0.25, steps=24) {
    dz = h / steps;
    for (i = [0:steps-1]) {
        t1 = i / steps;
        t2 = (i + 1) / steps;
        z1 = i * dz;
        z2 = z1 + dz + 0.02;
        
        ripple1 = ripple_amp * sin(t1 * 360 * 3.5);
        ripple2 = ripple_amp * sin(t2 * 360 * 3.5);
        
        r1 = (r_base * (1 - pow(t1, 0.75)) + r_tip) + ripple1;
        r2 = (r_base * (1 - pow(t2, 0.75)) + r_tip) + ripple2;
        
        dx1 = curve_amp * sin(t1 * 180) * cos(curve_dir);
        dy1 = curve_amp * sin(t1 * 180) * sin(curve_dir);
        dx2 = curve_amp * sin(t2 * 180) * cos(curve_dir);
        dy2 = curve_amp * sin(t2 * 180) * sin(curve_dir);
        
        hull() {
            translate([dx1, dy1, z1]) cylinder(r=max(0.4, r1), h=0.02, center=true, $fn=16);
            translate([dx2, dy2, z2]) cylinder(r=max(0.4, r2), h=0.02, center=true, $fn=16);
        }
    }
}

// =============================================================================
// SUB-MODULE: Icicle Cluster Diffuser Cap (Print Ready, Sealed Plafond)
// =============================================================================
module icicle_cluster_cap() {
    icicles = [
        [ 0.6,  0.3, 42.0, 4.8, 0.8, 2.0,  40, 26], // Central
        [ 4.2,  2.0, 34.5, 4.0, 0.7, 1.5,  75, 22], // Front-right
        [-4.0,  1.8, 29.5, 3.8, 0.6, 1.2, 135, 20], // Front-left
        [ 0.3, -4.0, 36.5, 4.2, 0.7, 1.6, 250, 24], // Back
        [-3.0, -2.8, 23.5, 3.2, 0.5, 1.0, 210, 16]  // Minor
    ];

    color(c_cap)
    difference() {
        union() {
            // 1. Lower collar with smooth rounded shoulder fillet R=2.0mm (Z = 0..6.5 mm)
            // Eliminates any flat horizontal cylinder top!
            rotate_extrude($fn=64) {
                polygon([
                    [0, 0],
                    [base_od/2, 0],
                    [base_od/2, 4.4],       // full 17.6mm OD protecting snap groove (wall = 1.425mm)
                    [base_od/2 - 0.2, 5.0], // smooth rounded fillet
                    [base_od/2 - 0.7, 5.6],
                    [base_od/2 - 1.4, 6.2], // r = 7.4mm
                    [6.8, 6.5],
                    [0, 6.5]
                ]);
            }

            // 2. Downward icicle streams continuing onto collar base (Z=0.4mm), fading thickness to 0!
            for (i = [1:4]) {
                ic = icicles[i];
                hull() {
                    // At bottom of collar (Z=0.4mm): thickness is exactly 0 (conformal to collar cylinder)
                    intersection() {
                        cylinder(d=base_od + 0.05, h=0.4, $fn=64);
                        translate([ic[0], ic[1], 0])
                            cylinder(r=ic[3], h=1.0, center=true, $fn=20);
                    }
                    // At Z=6.0mm: full icicle root circle
                    translate([ic[0], ic[1], 6.0])
                        cylinder(r=ic[3], h=0.2, center=true, $fn=20);
                }
            }

            // 3. Cluster of 5 natural wavy icicles starting at Z = 6.0 mm
            for (ic = icicles) {
                translate([ic[0], ic[1], 6.0])
                    single_icicle(h=ic[2], r_base=ic[3], r_tip=ic[4], curve_amp=ic[5], curve_dir=ic[6], steps=ic[7]);
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

        // 3. Optical light cavity directing LED light into all 5 icicle roots
        translate([0, 0, neck_h])
            cylinder(r1=neck_od/2 - wall_thickness, r2=3.0, h=4.0);
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
        icicle_cluster_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=cluster_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_mount();
    translate([0, 0, base_bottom_th + cable_slot_h / 2])
        wire_harness_dummy(base_od + 14);
    translate([0, 0, base_flange_h + neck_h - pocket_depth])
        led_pcb_dummy();
    translate([0, 0, base_flange_h])
        icicle_cluster_cap();
}

module cutaway_view() {
    difference() {
        assembled_view();
        translate([0, 0, -1])
            cube([25, 25, cluster_height + 15]);
    }
}

if (view_mode == "exploded") {
    exploded_assembly();
} else if (view_mode == "assembled") {
    assembled_view();
} else if (view_mode == "cutaway") {
    cutaway_view();
} else if (view_mode == "cap_only") {
    icicle_cluster_cap();
} else if (view_mode == "base_only") {
    base_mount();
}
