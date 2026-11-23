/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric Natural Icicle Cluster Diffuser Cap (50mm Standard)
 *          on the two-part base mount of base_mount.scad.
 *          Fully optimized for FDM 3D printing with 0.40mm nozzle and 0.20mm layers.
 * Contributors: Anton Bespalov
 */

include <base_interface.scad>
use <base_mount.scad>

/* [Display & Animation Options] */
view_mode = "assembled"; // [exploded: Exploded Assembly View, assembled: Full Assembled Figurine, cutaway: 90-Degree Cross Section, cap_only: Diffuser Cap (Print Ready)]
explode_gap = 24;

$fn = 48;

/* [FDM Nozzle 0.4mm & Layer 0.20mm Slicing Parameters] */
nozzle_w        = 0.40;
line_w_outer    = 0.42;
line_w_inner    = 0.44;
wall_thickness  = 1.40; // 3-4 solid perimeters (minimum 1.40 mm everywhere)

/* [Figurine] */
cluster_height    = 240 * layer_h;// 48.00 mm

c_cap       = [0.90, 0.94, 0.96, 0.80]; // Bambu PETG Translucent Clear

// =============================================================================
// SUB-MODULE: Natural Wavy Stalactite / Icicle Generator
// =============================================================================
// inset > 0 draws the icicle's hollow: each section narrowed by inset, and none where
// that leaves less than 0.3 mm.
module single_icicle(h=32, r_base=4.8, r_tip=0.8, curve_amp=1.8, curve_dir=0, ripple_amp=0.25, steps=24, inset=0) {
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
        
        if (inset == 0)
            hull() {
                translate([dx1, dy1, z1]) cylinder(r=max(0.4, r1), h=0.02, center=true, $fn=16);
                translate([dx2, dy2, z2]) cylinder(r=max(0.4, r2), h=0.02, center=true, $fn=16);
            }
        else if (r1 - inset >= 0.3)
            hull() {
                translate([dx1, dy1, z1]) cylinder(r=r1 - inset, h=0.02, center=true, $fn=16);
                // the last section closes to a point, so the tube ends in a cone
                if (r2 - inset >= 0.3)
                    translate([dx2, dy2, z2]) cylinder(r=r2 - inset, h=0.02, center=true, $fn=16);
                else
                    translate([dx2, dy2, z2 + r1 - inset]) cylinder(r=0.01, h=0.02, center=true, $fn=8);
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

        // 3. The hollow: a cone out of the socket bore into the icicles, and every icicle
        // a tube eroded by icicle_wall down to where it is too thin to hold one, so the
        // light reaches into each, where they were solid with a short chamber over the
        // socket. The hollow starts a wall above the socket's end, and each tube closes
        // to its icicle's tip, so it has no ceiling to support.
        translate([0, 0, neck_h])
            cylinder(r1=neck_od/2 - wall_thickness, r2=3.0, h=4.0);
        intersection() {
            union() {
                for (ic = icicles)
                    translate([ic[0], ic[1], 6.0])
                        single_icicle(h=ic[2], r_base=ic[3], r_tip=ic[4], curve_amp=ic[5],
                                      curve_dir=ic[6], steps=ic[7], inset=icicle_wall);
            }
            translate([0, 0, neck_h + socket_axial_gap + icicle_wall]) cylinder(r=30, h=cluster_height);
        }
    }
}

icicle_wall = 1.50;

// =============================================================================
// MAIN ASSEMBLY CONTROLLER
// =============================================================================
module exploded_assembly() {
    base_assembly(explode_gap);
    translate([0, 0, base_flange_h + explode_gap * 1.25])
        icicle_cluster_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=cluster_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_assembly();
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
}
