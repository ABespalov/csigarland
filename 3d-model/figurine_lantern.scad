/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric Vintage Holiday Lantern Diffuser Cap (50mm Standard)
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

$fn = 36;

/* [FDM Nozzle 0.4mm & Layer 0.20mm Slicing Parameters] */
nozzle_w        = 0.40;
line_w_outer    = 0.42;
line_w_inner    = 0.44;
wall_thickness  = 1.40; // 3-4 solid perimeters (minimum 1.40 mm everywhere)

/* [Figurine] */
lantern_height    = 237.5 * layer_h; // 47.50 mm

c_cap       = [0.52, 0.54, 0.58, 0.85]; // Bambu PETG Translucent Gray

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

        // 3. The hollow, from the neck top, where it opens out of the socket bore
        intersection() {
            shell_solid(function (a, z) shell_ray_union(lantern_hollow(), a, z),
                        neck_h, lantern_height, dz = 0.2, na = 120, ceiling = 35);
            translate([0, 0, neck_h]) cylinder(r = 30, h = lantern_height);
        }
    }
}

// The hollow: canopy, eaves, window cage, base tray and finial, each eroded by
// lantern_wall across its slope, so the lantern is an even shell that glows alike all
// over (see figurine_shell.scad), where its walls were 2 to 3 mm and the tray and finial
// solid. The corner columns and the window reliefs are too thin to hollow and stay
// solid. Under the flat eaves, as the lantern prints, the hollow closes at 35 deg.
lantern_wall = 1.50;
// A hexagonal frustum's hollow: circumradii r0 to r1 over z0..z1, eroded across its slope.
function lantern_hex(z0, z1, r0, r1) =
    let (k = lantern_wall / cos(atan((r0 - r1) * cos(30) / (z1 - z0))) / cos(30))
    ["p", z0, z1, r0 - k, r1 - k, 6, 0];
function lantern_hollow() =
    let (w = lantern_wall)
    concat(
        [["f", 4.4, 5.2, [0, 0], [0, 0], base_od / 2 - w / cos(atan(1.2 / 0.81)), 7.6 - w / cos(atan(1.2 / 0.81))],
         lantern_hex(5.2, 12.0, 17.6 / 2, 26.4 / 2),     // canopy
         lantern_hex(12.0, 13.5, 27.2 / 2, 27.2 / 2),    // eaves
         lantern_hex(13.5, 32.5, 22.4 / 2, 19.6 / 2),    // window cage
         lantern_hex(32.5, 36.3, 21.4 / 2, 16.8 / 2),    // base tray
         lantern_hex(36.3, 38.8, 16.8 / 2, 11.0 / 2),
         ["f", 38.5, 41.3, [0, 0], [0, 0], 4.8 - w / cos(atan(2.3 / 2.8)), 2.5 - w / cos(atan(2.3 / 2.8))]],
        shell_ellipsoid([0, 0, 42.8], [3.4, 3.4, 3.4 * 1.4] - [w, w, w]));   // finial drop

// =============================================================================
// MAIN ASSEMBLY CONTROLLER
// =============================================================================
module exploded_assembly() {
    base_assembly(explode_gap);
    translate([0, 0, base_flange_h + explode_gap * 1.25])
        lantern_diffuser_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=lantern_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_assembly();
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
}
