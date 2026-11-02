/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric 4-Tier Spiral Christmas Tree Diffuser & Universal Base Mount (50mm Standard)
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
wall_thickness  = 1.50; // 3-4 solid perimeters (minimum 1.50 mm everywhere)

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
tree_height       = 240 * layer_h;// 48.00 mm

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

c_cap       = [0.20, 0.75, 0.45, 0.85]; // Translucent emerald pine PETG
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
// SUB-MODULE: 4-Tier Spiral Christmas Tree Diffuser Cap
// =============================================================================
collar_h    = 22 * layer_h;  // 4.40 mm
tip_z       = 36 * layer_h;  // 7.20 mm: collar tapers down to the tree top here
tip_r       = 5.80;          // wall around the chamber stays over 1.50 mm
lobe_ratio  = 0.12;
tier_twist  = 26;
trunk_z     = 42.0;
trunk_r     = 3.00;
tree_h      = tree_height;

// [z start, z rim, z end, mean r start, mean r rim, mean r end]
tiers = [
    [ 6.8, 13.4, 14.8,  5.6,  7.6,  6.0],
    [14.8, 23.4, 24.8,  6.0, 10.6,  8.2],
    [24.8, 32.6, 34.0,  8.2, 13.4, 11.0],
    [34.0, 41.0, 42.4, 11.0, 15.9, 13.4]
];

function star6_pts(r_mean, r_amp) = [
    for (a = [0 : 7.5 : 359])
        let (r = r_mean + r_amp * cos(6 * a))
        [r * cos(a), r * sin(a)]
];

// One lobed section whose mean radius goes from r0 to r1; lobes scale with it.
module lobed_section(z0, z1, r0, r1, rot0, twist) {
    translate([0, 0, z0])
        rotate([0, 0, rot0])
            linear_extrude(height=z1 - z0, twist=twist, scale=r1 / r0, slices=ceil((z1 - z0) * 2.5))
                polygon(points=star6_pts(r0, r0 * lobe_ratio));
}

// Flares out to the rim, then steps back in under it; twist is continuous across both.
// Printed collar-down, so the flare is the overhang and the step back is the wall leaning in.
module tree_tier(z0, z_rim, z1, r0, r_rim, r1, rot0, twist) {
    twist_rim = twist * (z_rim - z0) / (z1 - z0);
    lobed_section(z0, z_rim, r0, r_rim, rot0, twist_rim);
    if (z1 > z_rim)
        lobed_section(z_rim, z1, r_rim, r1, rot0 - twist_rim, twist - twist_rim);
}

module tree_trunk() {
    rotate_extrude($fn=32) {
        polygon(points=[
            [0, trunk_z],
            [trunk_r, trunk_z],
            [trunk_r, tree_h - 0.6],
            [trunk_r - 0.6, tree_h],
            [0, tree_h]
        ]);
    }
}

module tree_diffuser_cap() {
    color(c_cap)
    difference() {
        union() {
            // 1. Collar, tapering to the narrow tree top right below the socket
            cylinder(d=base_od, h=collar_h, $fn=64);
            translate([0, 0, collar_h])
                cylinder(r1=base_od / 2, r2=tip_r, h=tip_z - collar_h, $fn=64);

            // 2. Core: keeps the wall around the light chamber in the tier valleys
            cylinder(r=tip_r, h=tiers[1][0] + 1.0);

            // 3. Tiers, each wider than the one before. The twist alternates and each tier
            // starts where the last one ended, so the lobes meet with no downward ledge.
            for (i = [0 : len(tiers) - 1])
                let (t = tiers[i])
                    tree_tier(t[0], t[1], t[2], t[3], t[4], t[5], i % 2 == 0 ? 0 : -tier_twist, (i % 2 == 0 ? 1 : -1) * tier_twist);

            // 4. Trunk
            tree_trunk();
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

        // 3. Hollow light-diffusion chamber inside core
        translate([0, 0, neck_h])
            cylinder(r1=4.4, r2=3.0, h=4.0);
        translate([0, 0, neck_h + 3.9])
            cylinder(r1=3.0, r2=1.8, h=16.0);
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
        tree_diffuser_cap();
    %color([0.5, 0.5, 0.6, 0.35])
        translate([0, 0, -2])
            cylinder(d=0.4, h=tree_height + explode_gap * 1.35, $fn=12);
}

module assembled_view() {
    base_mount();
    translate([0, 0, base_bottom_th + cable_slot_h / 2])
        wire_harness_dummy(base_od + 14);
    translate([0, 0, base_flange_h + neck_h - pocket_depth])
        led_pcb_dummy();
    translate([0, 0, base_flange_h])
        tree_diffuser_cap();
}

module cutaway_view() {
    difference() {
        assembled_view();
        translate([0, 0, -1])
            cube([25, 25, tree_height + 15]);
    }
}

if (view_mode == "exploded") {
    exploded_assembly();
} else if (view_mode == "assembled") {
    assembled_view();
} else if (view_mode == "cutaway") {
    cutaway_view();
} else if (view_mode == "cap_only") {
    tree_diffuser_cap();
} else if (view_mode == "base_only") {
    base_mount();
}
