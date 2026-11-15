/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric Gingerbread Man Diffuser Cap & Universal Base Mount (50mm Standard)
 *          Fully optimized for FDM 3D printing with 0.40mm nozzle and 0.20mm layers.
 * Contributors: Anton Bespalov
 */

use <figurine_shell.scad>

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
cookie_height     = 239.5 * layer_h; // 47.90 mm

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

c_cap       = [1.00, 0.52, 0.12, 0.88]; // Bambu PETG Translucent Orange
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

// =============================================================================
// SUB-MODULE: Gingerbread Man Diffuser Cap (Print Ready, Sealed Plafond)
// =============================================================================
module gingerbread_diffuser_cap() {
    z_head = 17.5;
    r_head = 7.5;

    color(c_cap)
    difference() {
        union() {
            // 1. Lower collar with steep, elegant taper into head (Z = 0..14.0 mm)
            // Tapers to r=3.5mm at Z=14.0mm so it enters COMPLETELY inside the head with zero step!
            rotate_extrude($fn=64) {
                polygon([
                    [0, 0],
                    [base_od/2, 0],
                    [base_od/2, 4.4],       // full 17.6mm OD protecting internal snap ridge (wall = 1.425mm)
                    [base_od/2 - 0.2, 5.2], // smooth rounded shoulder fillet R=2.0mm
                    [base_od/2 - 0.8, 6.0],
                    [base_od/2 - 1.6, 6.8], // steep taper begins
                    [5.6, 8.8],
                    [4.4, 11.0],
                    [3.7, 13.0],
                    [3.5, 14.0],
                    [0, 14.0]
                ]);
            }

            // 2. Head (plump round cookie)
            translate([0, 0, z_head])
                scale([1.0, 0.78, 1.0])
                    sphere(r=r_head, $fn=32);

            // Solid neck connector bridging head to torso (guarantees thick solid neck)
            translate([0, 0, 24.2])
                scale([1.0, 0.78, 1.0])
                    cylinder(r=5.5, h=2.5, center=true, $fn=24);

            // Gumdrop eyes (-Y)
            translate([-2.2, -5.4, z_head - 1.5])
                sphere(r=1.0, $fn=16);
            translate([2.2, -5.4, z_head - 1.5])
                sphere(r=1.0, $fn=16);

            // Protruding gumdrop / icing nose between eyes and smile (facing -Y)
            translate([0, -5.8, z_head - 0.1])
                sphere(r=1.2, $fn=16);

            // Cheerful happy smile arc (-Y) - corners curl UP towards eyes when hanging!
            for (ang = [-40, -20, 0, 20, 40]) {
                translate([3.0 * sin(ang), -5.5 + 0.35 * cos(ang), z_head + 1.2 + 0.8 * cos(ang * 1.8)])
                    sphere(r=0.6, $fn=12);
            }

            // 3. Neck & 3D Bow Tie (Z = 24.8 mm)
            translate([0, -5.0, 24.8]) {
                // Knot
                sphere(r=1.5, $fn=16);
                // Left loop
                translate([-2.4, 0.3, 0])
                    rotate([0, -15, 10])
                        scale([1.5, 0.7, 1.0])
                            sphere(r=1.3, $fn=14);
                // Right loop
                translate([2.4, 0.3, 0])
                    rotate([0, 15, -10])
                        scale([1.5, 0.7, 1.0])
                            sphere(r=1.3, $fn=14);
            }

            // 4. Torso (plump cookie body)
            hull() {
                translate([0, 0, 25.0])
                    scale([1.0, 0.78, 1.0])
                        cylinder(r=5.8, h=1.0, center=true);
                translate([0, 0, 34.0])
                    scale([1.1, 0.82, 1.0])
                        cylinder(r=6.6, h=1.0, center=true);
            }

            // Gumdrop buttons on belly
            for (bz = [28.5, 32.8]) {
                translate([0, -5.5, bz])
                    sphere(r=1.1, $fn=14);
            }

            // 5. Arms (welcoming open hug)
            // Left arm
            hull() {
                translate([-4.8, 0, 26.5]) sphere(r=3.2, $fn=16);
                translate([-14.2, -1.2, 30.0]) sphere(r=3.0, $fn=20);
            }
            // Left wavy icing wrist cuff
            translate([-12.2, -1.0, 29.5])
                rotate([0, 25, 0])
                    rotate_extrude($fn=20)
                        translate([2.8, 0])
                            circle(r=0.6, $fn=10);

            // Right arm
            hull() {
                translate([4.8, 0, 26.5]) sphere(r=3.2, $fn=16);
                translate([14.2, -1.2, 30.0]) sphere(r=3.0, $fn=20);
            }
            // Right wavy icing wrist cuff
            translate([12.2, -1.0, 29.5])
                rotate([0, -25, 0])
                    rotate_extrude($fn=20)
                        translate([2.8, 0])
                            circle(r=0.6, $fn=10);

            // 6. Legs
            // Left leg
            hull() {
                translate([-3.6, 0, 34.5]) sphere(r=3.6, $fn=16);
                translate([-7.2, 0, 44.5]) sphere(r=3.4, $fn=20);
            }
            // Left wavy icing ankle cuff
            translate([-6.5, 0, 41.5])
                rotate_extrude($fn=20)
                    translate([3.2, 0])
                        circle(r=0.6, $fn=10);

            // Right leg
            hull() {
                translate([3.6, 0, 34.5]) sphere(r=3.6, $fn=16);
                translate([7.2, 0, 44.5]) sphere(r=3.4, $fn=20);
            }
            // Right wavy icing ankle cuff
            translate([6.5, 0, 41.5])
                rotate_extrude($fn=20)
                    translate([3.2, 0])
                        circle(r=0.6, $fn=10);
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

        // 3. The hollow, from the neck top, where it opens out of the socket bore: the head,
        // neck and torso round the axis, and the arms and legs as tubes of their own
        intersection() {
            union() {
                shell_solid(function (a, z) shell_ray_union(cookie_hollow(), a, z),
                            neck_h, cookie_height, dz = 0.2, na = 96, ceiling = 35);
                for (s = [-1, 1]) {
                    cookie_limb([[[s * 4.8, 0, 26.5], 3.2], [[s * 14.2, -1.2, 30.0], 3.0]]);
                    // A leg's tube runs on up into the torso, past the flat underside
                    // where the torso's own hollow has closed.
                    cookie_limb([[[s * 3.4, 0, 31.5], 3.4], [[s * 3.6, 0, 34.5], 3.6], [[s * 7.2, 0, 44.5], 3.4]]);
                }
            }
            translate([0, 0, neck_h]) cylinder(r = 30, h = cookie_height);
        }
    }
}

// The hollow: collar taper, head, neck and torso, each eroded by cookie_wall, so the
// cookie is an even shell that glows alike all over (see figurine_shell.scad), where
// its walls were 2 to 3 mm with narrow channels into the limbs. The eyes, nose, smile,
// bow tie, buttons and icing cuffs are too thin to hollow and stay solid. Where the
// outline turns flat as the cookie prints, under the torso, the hollow closes at 35 deg.
cookie_wall = 1.50;
function cookie_hollow() =
    let (w = cookie_wall,
         // collar outline [z, r], each run eroded across its slope
         c = [[4.4, base_od / 2], [6.8, base_od / 2 - 1.6], [8.8, 5.6], [11.0, 4.4], [13.0, 3.7], [14.0, 3.5]])
    concat(
        [for (i = [0 : len(c) - 2])
            let (k = w / cos(atan((c[i][1] - c[i + 1][1]) / (c[i + 1][0] - c[i][0]))))
                ["f", c[i][0], c[i + 1][0], [0, 0], [0, 0], c[i][1] - k, c[i + 1][1] - k]],
        shell_ellipsoid([0, 0, 17.5], [7.5, 7.5 * 0.78, 7.5] - [w, w, w]),          // head
        [["x", 22.95, 25.45, 5.5 - w, 5.5 * 0.78 - w, 5.5 - w, 5.5 * 0.78 - w],    // neck
         ["x", 24.5, 34.5 - w, 5.8 - w, 5.8 * 0.78 - w, 6.6 * 1.1 - w, 6.6 * 0.82 - w]]); // torso

// A limb's hollow: the hull of its spheres [[centre, radius], ...] eroded by the wall,
// with a ridge on top at 35 deg from the horizontal, so the tube prints with no support
// inside it.
module cookie_limb(balls) {
    for (i = [0 : len(balls) - 2])
        hull() {
            for (b = [balls[i], balls[i + 1]]) {
                translate(b[0]) sphere(r = b[1] - cookie_wall, $fn = 24);
                translate(b[0] + [0, 0, (b[1] - cookie_wall) / cos(35)]) sphere(r = 0.05, $fn = 6);
            }
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
        gingerbread_diffuser_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=cookie_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_mount();
    translate([0, 0, base_bottom_th + cable_slot_h / 2])
        wire_harness_dummy(base_od + 14);
    translate([0, 0, base_flange_h + neck_h - pocket_depth])
        led_pcb_dummy();
    translate([0, 0, base_flange_h])
        gingerbread_diffuser_cap();
}

module cutaway_view() {
    difference() {
        assembled_view();
        translate([0, 0, -1])
            cube([25, 25, cookie_height + 15]);
    }
}

if (view_mode == "exploded") {
    exploded_assembly();
} else if (view_mode == "assembled") {
    assembled_view();
} else if (view_mode == "cutaway") {
    cutaway_view();
} else if (view_mode == "cap_only") {
    gingerbread_diffuser_cap();
} else if (view_mode == "base_only") {
    base_mount();
}
