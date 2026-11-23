/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric classic Holiday Bell Diffuser Cap (50mm Standard)
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
bell_height       = 245 * layer_h;// 49.00 mm, the clapper peeking out of the mouth

c_cap       = [0.15, 0.60, 0.62, 0.88]; // Bambu PETG Translucent Teal

// =============================================================================
// SUB-MODULE: Holiday Bell Diffuser Cap (Print Ready)
// =============================================================================
// A classic church bell, drawn in the cap's own frame: Z = 0 is the collar on the
// base flange, the top of the bell as it hangs, and Z grows towards the mouth,
// which is up on the print bed. The outline (r, z) is a chain of cubic Bezier
// segments: a rounded crown shoulder, a nearly straight waist, the flare and a
// thick rolled lip (the sound bow). No printed surface is flatter than 35 deg
// from the horizontal, but the socket ceiling inside the collar, and the
// interior is cut as a shell of bell_wall normal to the outline.

bell_wall     = 1.50;  // shell normal to the outline, 3-4 perimeters
bell_lip_z    = 48.0;  // top face of the lip, the mouth as the bell hangs
bell_mouth_r  = 16.8;  // where the lip's outline meets its top face
bell_apex_z   = 37.0;  // apex of the conical mouth ceiling, on the axis: a short clapper
bell_plate    = 1.60;  // that ceiling's thickness, normal to its 45 deg cone
clapper_z     = 45.5;  // centre of the clapper ball, which peeks out of the mouth
clapper_r     = 3.5;

// Outline segments: [start, control, control, end].
bell_segments = [
    [[base_od/2, 4.4], [10.40, 5.52], [11.7, 8.4], [11.7, 12.0]],   // crown shoulder, 35 deg at the collar
    [[11.7, 12.0], [11.7, 15.5], [11.2, 18.5], [11.2, 22.0]],       // waist, narrowing a little
    [[11.2, 22.0], [11.2, 30.0], [14.4, 37.4], [18.6, 40.4]],       // flare, 35.5 deg at its end
    [[18.6, 40.4], [19.6, 41.1], [20.0, 43.3], [19.8, 45.3]],       // lip bulge, OD 40.0 mm
    // The lip rolls over to the mouth and meets it at 36 deg from the horizontal, so the
    // wall inside it runs parallel, with no flat ceiling that would want support.
    [[19.8, 45.3], [19.7, 46.4], [17.9, 47.2], [bell_mouth_r, bell_lip_z]]
];

function bez(s, t) = pow(1 - t, 3) * s[0] + 3 * pow(1 - t, 2) * t * s[1]
                   + 3 * (1 - t) * t * t * s[2] + t * t * t * s[3];
function bell_outline(n = 16) = concat(
    [[0, 0], [base_od/2, 0]],
    [for (s = bell_segments) for (i = [0 : n - 1]) bez(s, i / n)],
    [[bell_mouth_r, bell_lip_z]]
);

// The bell's solid, and the same solid running on past the mouth, mirrored over
// the axis, so that offsetting it inward neither leaves a core on the axis nor
// closes the mouth.
module bell_solid_2d() {
    polygon(concat(bell_outline(), [[0, bell_lip_z]]));
}
module bell_open_2d() {
    union() {
        for (m = [0, 1]) mirror([m, 0])
            polygon(concat(bell_outline(), [[bell_mouth_r, bell_lip_z + 20], [0, bell_lip_z + 20]]));
    }
}

module bell_shell_2d() {
    roof_z = bell_apex_z - bell_plate * sqrt(2);  // apex of the light chamber's roof
    // The chamber opens straight out of the socket bore and widens at 35 deg from the
    // horizontal, as the crown shoulder does, so the shoulder keeps an even wall and
    // the socket has no ceiling to support.
    k = 1 / tan(35);
    socket_r = (neck_od + neck_socket_gap) / 2;
    r0 = socket_r - socket_axial_gap * k;          // at the neck top, socket_r at the socket's end
    rc = (r0 + k * (roof_z - neck_h)) / (1 + k);   // where the widening meets the 45 deg roof
    difference() {
        bell_solid_2d();
        intersection() {
            offset(r = -bell_wall) bell_open_2d();
            union() {
                // The mouth, down to its 45 deg conical ceiling.
                polygon([[0, bell_apex_z], [40, bell_apex_z - 40], [40, 80], [0, 80]]);
                // The light chamber above the LED closes under a 45 deg roof,
                // so nothing inside needs support.
                polygon([[0, neck_h], [r0, neck_h], [rc, roof_z - rc], [0, roof_z]]);
            }
        }
    }
}

// The clapper: a tapered rod from the apex of the mouth ceiling, ending in a ball
// that a 45 deg cone carries, so it prints without support.
module bell_clapper_2d() {
    tangent = [clapper_r * cos(45), clapper_z - clapper_r * sin(45)];
    intersection() {
        union() {
            translate([0, clapper_z]) circle(r = clapper_r, $fn = 40);
            // The rod's foot sits below the cone of the mouth ceiling all round.
            polygon([[0, bell_apex_z - 3.4], [2.2, bell_apex_z - 3.4],
                     [1.6, tangent[1] - (tangent[0] - 1.6)], tangent, [0, tangent[1]]]);
        }
        translate([0, 0]) square([20, 80]);
    }
}

module bell_diffuser_cap() {
    color(c_cap)
    difference() {
        rotate_extrude($fn = 96, convexity = 10) {
            bell_shell_2d();
            bell_clapper_2d();
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
    }
}

// =============================================================================
// MAIN ASSEMBLY CONTROLLER
// =============================================================================
module exploded_assembly() {
    base_assembly(explode_gap);
    translate([0, 0, base_flange_h + explode_gap * 1.25])
        bell_diffuser_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=bell_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_assembly();
    translate([0, 0, base_flange_h])
        bell_diffuser_cap();
}

module cutaway_view() {
    difference() {
        assembled_view();
        translate([0, 0, -1])
            cube([25, 25, bell_height + 15]);
    }
}

if (view_mode == "exploded") {
    exploded_assembly();
} else if (view_mode == "assembled") {
    assembled_view();
} else if (view_mode == "cutaway") {
    cutaway_view();
} else if (view_mode == "cap_only") {
    bell_diffuser_cap();
}
