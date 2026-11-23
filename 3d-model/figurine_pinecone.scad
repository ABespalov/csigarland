/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric Natural Pinecone Holiday Diffuser Cap (50mm Standard)
 *          on the two-part base mount of base_mount.scad.
 *          Fully optimized for FDM 3D printing with 0.40mm nozzle and 0.20mm layers.
 * Contributors: Anton Bespalov
 */

include <base_interface.scad>
use <base_mount.scad>

/* [Display & Animation Options] */
view_mode = "assembled"; // [exploded: Exploded Assembly View, assembled: Full Assembled Figurine, cutaway: 90-Degree Cross Section, cap_only: Diffuser Cap (Print Ready)]
explode_gap = 24;

$fn = 40;

/* [FDM Nozzle 0.4mm & Layer 0.20mm Slicing Parameters] */
nozzle_w        = 0.40;
line_w_outer    = 0.42;
line_w_inner    = 0.44;
wall_thickness  = 1.50; // 3-4 solid perimeters (minimum 1.50 mm everywhere)

/* [Figurine] */
cone_height       = 228.5 * layer_h; // 45.70 mm

c_cap       = [0.58, 0.36, 0.20, 0.88]; // Bambu PETG Translucent Brown

// =============================================================================
// SUB-MODULE: Natural Pinecone Diffuser Cap (Print Ready, Sealed Plafond)
// =============================================================================
module pinecone_diffuser_cap() {
    color(c_cap)
    difference() {
        union() {
            // Lower collar with rounded upper edge + narrow stem transition (Z = 0..9.5 mm)
            rotate_extrude($fn=48) {
                polygon([
                    [0, 0],
                    [base_od/2, 0],
                    [base_od/2, 4.4],        // full 17.6mm OD protecting snap groove (wall = 1.425mm)
                    [base_od/2 - 0.2, 5.2],  // smooth rounded fillet R=2.0mm
                    [base_od/2 - 0.8, 6.0],
                    [7.2, 7.0],
                    [5.6, 9.5],
                    [0, 9.5]
                ]);
            }

            // Solid central tapered core connecting collar to crown (ZERO GAPS!)
            translate([0, 0, 9.0])
                cylinder(r1=5.5, r2=10.2, h=24.0);
            translate([0, 0, 33.0])
                cylinder(r1=10.2, r2=1.5, h=11.5);

            // Narrow rounded stem transition at collar
            translate([0, 0, 9.2])
                sphere(r=4.6, $fn=24);

            // 8 main body tiers of scales in checkerboard pattern (opening outward)
            tiers = 8;
            scales = 8;
            for (i = [0:tiers-1]) {
                t = i / (tiers - 1);
                z = 10.0 + i * 3.8;
                r = 10.2 * sin(t * 115 + 16) + 4.2;
                rot = (i % 2) * (360 / scales / 2);
                for (s = [0:scales-1]) {
                    rotate([0, 0, rot + s * (360/scales)])
                        translate([r * 0.72, 0, z])
                            rotate([0, -(12 + t * 25), 0])
                                scale([1.25, 0.95, 0.45])
                                    sphere(r=r * 0.46, $fn=14);
                }
            }

            // Tier 9: Converging upper crown scales (smaller, tilted inward 42 deg)
            z9 = 10.0 + 7 * 3.8 + 2.8; // Z = 39.4
            rot9 = (8 % 2) * (360 / 8 / 2);
            for (s = [0:7]) {
                rotate([0, 0, rot9 + s * 45])
                    translate([6.2, 0, z9])
                        rotate([0, -42, 0])
                            scale([1.2, 0.9, 0.45])
                                sphere(r=4.0, $fn=14);
            }

            // Tier 10: Rosette crown tier (6 smaller petal scales closing the crown)
            z10 = z9 + 2.2; // Z = 41.6
            for (s = [0:5]) {
                rotate([0, 0, s * 60 + 15])
                    translate([3.4, 0, z10])
                        rotate([0, -60, 0])
                            scale([1.1, 0.85, 0.45])
                                sphere(r=3.2, $fn=12);
            }

            // Tier 11: Center 4 terminal rosette petals
            z11 = z10 + 1.8; // Z = 43.4
            for (s = [0:3]) {
                rotate([0, 0, s * 90 + 45])
                    translate([1.6, 0, z11])
                        rotate([0, -75, 0])
                            scale([1.0, 0.8, 0.45])
                                sphere(r=2.4, $fn=10);
            }

            // Terminal center umbo rosette button (natural closed tip - NO bare hemisphere!)
            translate([0, 0, z11 + 0.6])
                scale([1.0, 1.0, 0.7])
                    sphere(r=2.2, $fn=16);
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

        // 3. Continuous hollow diffusion chamber inside (strictly inside solid core, wall >= 1.5mm)
        translate([0, 0, neck_h])
            cylinder(r1=4.2, r2=2.8, h=4.0);
        translate([0, 0, neck_h + 3.9])
            cylinder(r1=2.8, r2=5.2, h=16.0);
        translate([0, 0, neck_h + 19.8])
            cylinder(r1=5.2, r2=2.0, h=9.0);
    }
}

// =============================================================================
// MAIN ASSEMBLY CONTROLLER
// =============================================================================
module exploded_assembly() {
    base_assembly(explode_gap);
    translate([0, 0, base_flange_h + explode_gap * 1.25])
        pinecone_diffuser_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=cone_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_assembly();
    translate([0, 0, base_flange_h])
        pinecone_diffuser_cap();
}

module cutaway_view() {
    difference() {
        assembled_view();
        translate([0, 0, -1])
            cube([25, 25, cone_height + 15]);
    }
}

if (view_mode == "exploded") {
    exploded_assembly();
} else if (view_mode == "assembled") {
    assembled_view();
} else if (view_mode == "cutaway") {
    cutaway_view();
} else if (view_mode == "cap_only") {
    pinecone_diffuser_cap();
}
