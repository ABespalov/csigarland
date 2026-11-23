/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric 3D Faceted Magen David Diffuser Cap (50mm Standard)
 *          on the two-part base mount of base_mount.scad.
 *          Fully optimized for FDM 3D printing with 0.40mm nozzle and 0.20mm layers.
 * Contributors: Anton Bespalov
 */

include <base_interface.scad>
use <base_mount.scad>

/* [Display & Animation Options] */
view_mode = "assembled"; // [exploded: Exploded Assembly View, assembled: Full Assembled Figurine, cutaway: 90-Degree Cross Section, cap_only: Diffuser Cap (Print Ready)]
explode_gap = 24;

$fn = 64;

/* [FDM Nozzle 0.4mm & Layer 0.20mm Slicing Parameters] */
nozzle_w        = 0.40;
line_w_outer    = 0.42;
line_w_inner    = 0.44;
wall_thickness  = 1.50; // 3-4 solid perimeters (minimum 1.50 mm everywhere)

/* [Figurine] */
magen_height      = 245 * layer_h;// 49.00 mm

c_cap       = [0.55, 0.78, 0.98, 0.85]; // Bambu PETG Translucent Light Blue

// =============================================================================
// SUB-MODULE: 3D Faceted Magen David Core Geometry
// =============================================================================
function tri_2d(r, rot=0) = [
    [r * cos(rot), r * sin(rot)],
    [r * cos(rot + 120), r * sin(rot + 120)],
    [r * cos(rot + 240), r * sin(rot + 240)]
];

module tri_faceted(r, th, rot=0, z0=28.0) {
    pts = tri_2d(r, rot);
    hull() {
        translate([0, 0, z0])
            rotate([90, 0, 0])
                linear_extrude(height=0.4, center=true)
                    polygon(pts);
        translate([0, th, z0]) sphere(r=0.5, $fn=16);
        translate([0, -th, z0]) sphere(r=0.5, $fn=16);
    }
}

module magen_david_3d(r=21.0, th=7.5, z0=28.0) {
    union() {
        tri_faceted(r=r, th=th, rot=90, z0=z0);  // Triangle pointing UP
        tri_faceted(r=r, th=th, rot=270, z0=z0); // Triangle pointing DOWN
    }
}

// Inner cavity of one faceted triangle: the same bipyramid with every face moved inward by
// `wall`. A face leans alpha = atan(th / (r/2)) from the star plane, so its edge line moves
// in by wall / sin(alpha) and the apex by wall / cos(alpha).
module tri_cavity(r, th, wall, rot=0, z0=28.0) {
    alpha = atan(th / (r / 2));
    r_in  = r - 2 * wall / sin(alpha);
    th_in = th - wall / cos(alpha);
    hull() {
        translate([0, 0, z0])
            rotate([90, 0, 0])
                linear_extrude(height=0.02, center=true)
                    polygon(tri_2d(r_in, rot));
        translate([0, th_in, z0]) sphere(r=0.05, $fn=8);
        translate([0, -th_in, z0]) sphere(r=0.05, $fn=8);
    }
}

// =============================================================================
// SUB-MODULE: Magen David Diffuser Cap (Print Ready, Sealed Plafond)
// =============================================================================
module magen_david_cap() {
    r_tri = 21.0;
    th_apex = 7.5;
    z0 = 28.0;
    star_wall = 1.2;  // shell thickness normal to the facets (~3 perimeters in plan)

    color(c_cap)
    difference() {
        union() {
            // 1. Lower collar with rounded upper edge (Z = 0..6.5 mm)
            rotate_extrude($fn=64) {
                polygon([
                    [0, 0],
                    [base_od/2, 0],
                    [base_od/2, 4.4],        // full 17.6mm cylinder protecting snap ridge (wall = 1.425mm)
                    [base_od/2 - 0.2, 5.2],  // continuous smooth rounded fillet R=2.0mm
                    [base_od/2 - 0.8, 6.0],
                    [base_od/2 - 1.6, 6.6],  // r = 7.2mm
                    [0, 6.6]
                ]);
            }

            // 2. Smooth solid loft blending collar neck directly into the star lower ray (Z = 6.2..16.0 mm)
            // Uses a smooth ellipsoid at top so the entire upper rim of the collar is completely rounded!
            hull() {
                translate([0, 0, 6.2])
                    cylinder(r=base_od/2 - 1.4, h=0.4, $fn=64);
                translate([0, 0, 14.5])
                    scale([1.15, 0.65, 1.0])
                        sphere(r=4.5, $fn=32);
            }

            // 3. Two intersecting 3D faceted triangles forming the 50mm Magen David
            magen_david_3d(r=r_tri, th=th_apex, z0=z0);
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

        // 3. LED clearance pocket: chip + capacitor stand 1.20 mm above the neck top
        //    (half-diagonal 3.54 mm, capacitor up to r = 3.7 mm); 45-degree roof, no bridge
        translate([0, 0, neck_h - 0.1])
            cylinder(r=4.4, h=2.3, $fn=32);
        translate([0, 0, neck_h + 2.2])
            cylinder(r1=4.4, r2=2.6, h=1.8, $fn=32);

        // 3a. Narrow optical light channel from clearance pocket to central chamber (Z = 8.8..21.5 mm)
        translate([0, 0, neck_h])
            cylinder(r1=2.8, r2=1.8, h=16.5, $fn=24);

        // 4. Hollow star: an even shell (star_wall) that follows every facet. The former
        //    ellipsoid chamber had near-flat floor and roof, and with 0% infill the solid
        //    rays left random voids; both printed as floating walls that showed through the
        //    translucent PETG near the centre. All facets lean 54-73 degrees, so this cavity
        //    has no flat ceilings.
        tri_cavity(r=r_tri, th=th_apex, wall=star_wall, rot=90, z0=z0);
        tri_cavity(r=r_tri, th=th_apex, wall=star_wall, rot=270, z0=z0);

        // 5. Hollow neck loft: the same shell inside the collar-to-ray blend
        hull() {
            translate([0, 0, 6.6])
                cylinder(r=base_od/2 - 1.4 - star_wall, h=0.1, $fn=64);
            translate([0, 0, 14.5])
                scale([(4.5 * 1.15 - star_wall) / 4.5, (4.5 * 0.65 - star_wall) / 4.5, (4.5 - star_wall) / 4.5])
                    sphere(r=4.5, $fn=32);
        }
    }
}

// =============================================================================
// MAIN ASSEMBLY CONTROLLER
// =============================================================================
module exploded_assembly() {
    base_assembly(explode_gap);
    translate([0, 0, base_flange_h + explode_gap * 1.25])
        magen_david_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=magen_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_assembly();
    translate([0, 0, base_flange_h])
        magen_david_cap();
}

module cutaway_view() {
    difference() {
        assembled_view();
        translate([0, 0, -1])
            cube([25, 25, magen_height + 15]);
    }
}

if (view_mode == "exploded") {
    exploded_assembly();
} else if (view_mode == "assembled") {
    assembled_view();
} else if (view_mode == "cutaway") {
    cutaway_view();
} else if (view_mode == "cap_only") {
    magen_david_cap();
}
