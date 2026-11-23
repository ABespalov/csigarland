/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric Cozy Winter Snowman Diffuser Cap (50mm Standard)
 *          on the two-part base mount of base_mount.scad.
 *          Fully optimized for FDM 3D printing with 0.40mm nozzle and 0.20mm layers.
 * Contributors: Anton Bespalov
 */

use <figurine_shell.scad>

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
wall_thickness  = 1.40; // 3-4 solid perimeters (minimum 1.40 mm everywhere)

/* [Figurine] */
snowman_height    = 238 * layer_h;// 47.60 mm

c_cap       = [0.92, 0.95, 0.97, 0.85]; // Bambu PETG Translucent Clear

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

        // 3. The hollow, from the neck top, where it opens out of the socket bore
        intersection() {
            shell_solid(function (a, z) shell_ray_union(snowman_hollow(), a, z),
                        neck_h, snowman_height, dz = 0.2, na = 96, ceiling = 35);
            translate([0, 0, neck_h]) cylinder(r = 30, h = snowman_height);
        }
    }
}

// The hollow: the hat, head, scarf, body, arms and the round at the bottom, each eroded
// by snowman_wall, so the snowman is an even shell that glows alike all over (see
// figurine_shell.scad). The eyes, nose, smile, buttons and scarf tails are too thin to
// hollow and stay solid. Where the outline turns flat, at the top of the head and the
// bottom of the body as the snowman prints, the hollow closes at 35 deg instead.
snowman_wall = 1.50;
function snowman_hollow() =
    let (w = snowman_wall, z_head = 19.5, r_head = 8.2, z_body = 35.0, r_body = 11.2)
    concat(
        // Collar shoulder and hat, their slopes taken in
        [["f", 4.4, 6.8, [0, 0], [0, 0], base_od / 2 - w / cos(atan(1.4 / 2.4)), 7.4 - w / cos(atan(1.4 / 2.4))],
         ["f", 6.8, 15.5, [0, 0], [0, 0], 7.4 - w, 8.2 - w],
         ["f", 25.2, 28.4, [0, 0], [0, 0], 5.8, 6.2],              // neck, inside the scarf
         ["t", 25.5, 7.0, 1.9 * 1.1 - w, 1.9 * 1.2 - w]],          // scarf
        shell_sphere([0, 0, z_head], r_head - w),
        shell_ellipsoid([2.4, -7.5, 26.5], [1.2, 0.85, 1.1] * 2.2 - [w, w, w]),   // scarf knot
        shell_ellipsoid([0, 0, z_body], [r_body, r_body, r_body * 0.96] - [w, w, w]),
        shell_sphere([0, 0, z_body + r_body * 0.96 - 0.6], 2.5 - w),
        [for (s = [-1, 1]) each shell_capsule([s * 8.5, 0, z_body - 2.4], 2.6 - w, [s * 12.0, -2.8, z_body - 1.2], 3.2 - w)]);

// =============================================================================
// MAIN ASSEMBLY CONTROLLER
// =============================================================================
module exploded_assembly() {
    base_assembly(explode_gap);
    translate([0, 0, base_flange_h + explode_gap * 1.25])
        snowman_diffuser_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=snowman_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_assembly();
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
}
