/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Parametric 3D printable enclosure for the csiGarland Control Unit (3-button layout)
 * Contributors: Anton Bespalov
 */

$fn = 48;

// =============================================================================
// PARAMETRIC SPECIFICATIONS (mm)
// Coordinates: X = length (AC bay at X=0, DC bay at X=box_L), Y = depth
// (front face at Y=0), Z = up. The lid top face is the control panel.
// =============================================================================

/* [Enclosure] */
box_L     = 122.0;  // Length (X): AC bay must fit Hi-Link + XT60 + fuse + MOV
box_W     = 68.0;   // Depth (Y)
base_H    = 32.0;   // Base tub height
lid_t     = 3.0;    // Lid plate thickness
box_H     = base_H + lid_t;  // 35.0 total
wall_t    = 2.5;
floor_t   = 2.5;
corner_r  = 4.0;
lip_h     = 2.5;    // Lid alignment lip depth into the tub
lip_w     = 1.2;
lip_clear = 0.2;
cradle_t  = 1.6;    // Wall thickness of internal cradles and sleeves

/* [Safety partition (AC | DC)] */
partition_x = 62.0;
partition_t = 2.0;
dc_x0       = partition_x + partition_t;

/* [Hi-Link HLK-10M05B] */
hilink_L         = 47.0;  // along X
hilink_W         = 28.0;  // along Y
hilink_H         = 22.0;
hilink_gap       = 0.25;  // per side -> 47.5 x 28.5 bay
hilink_x0        = 11.6;  // bay corner, clear of the corner screw bosses
hilink_y0        = 36.0;
hilink_standoff  = 4.0;   // corner shoulders: 4.0 mm pin plenum under the module
hilink_shoulder  = 4.0;
hilink_guide_h   = 10.0;  // corner guide pillars above the floor
hilink_guide_len = 6.0;
hilink_rib_h     = 2.6;   // lid ribs below the plate, set by the fit check; 0.9 mm above the nominal module top
hilink_bay       = [hilink_L + 2 * hilink_gap, hilink_W + 2 * hilink_gap];

/* [XT60H-F power inlet (left wall)] */
xt60_L      = 16.0;          // body + sheath along X (first print: 24 was 8 mm too deep)
xt60_pocket = [16.6, 9.4];   // form-fit slide chamber (Y, Z)
xt60_window = [15.8, 8.8];   // outer bezel stop (Y, Z)
xt60_cy     = 22.0;
xt60_z0     = floor_t + 3.5;
xt60_post_h = 15.8;          // lid post below the plate, set by the fit check

/* [Fuse holder and MOV (AC bay)] */
fuse_holder   = [30.0, 11.0, 12.0];  // enclosed 5x20 holder (X, Y, Z) - measure yours
fuse_x0       = 30.0;
fuse_y0       = 4.1;
fuse_cradle_h = 8.0;
mov_slot      = [5.0, 12.0];         // 10D471K disc: D11.5 x 4.5 standing on edge
mov_x0        = 32.6;
mov_y0        = 20.1;
mov_cradle_h  = 7.0;

/* [Waveshare ESP32-C6-LCD-1.3] */
esp_pcb       = [26.25, 30.0];  // Waveshare drawing
esp_total_h   = 8.95;           // glass front to the ends of the M2 standoffs
lcd_module    = [26.25, 29.42];
esp_cx        = 89.0;
esp_cy        = 45.0;           // USB-C end of the board points to +Y (unused, OTA only)
esp_hole_dx   = 22.55;          // M2 standoff pitch (measured on the board)
esp_hole_dy   = 25.50;
esp_post_d    = 7.0;            // top of the post, under the board
esp_post_slim = 6.0;            // height of the slim top
esp_post_base_d = 10.0;         // lower body; second print: the Ø8 mm posts snapped off under light pressure
esp_post_cone = 3.0;            // taper between the lower body and the slim top
esp_rib_reach = 7.5;            // gusset along Y, half length at the floor
esp_rib_h     = 10.0;           // gusset height above the floor
esp_rib_t     = 2.0;
esp_screw_d   = 2.4;            // M2 clearance
esp_bore_d    = 4.4;            // screwdriver/head bore from the bottom
esp_web       = 7.0;            // solid top clamped by an M2 x 10 screw; the bore ends in the thick part
lcd_aperture  = 25.0;
lcd_active_dx = -0.4;           // window offset from the board centre, measured on the first print
lcd_active_dy = -2.15;
lcd_trim_left = 1.4;            // second print: window edge moved in on the power inlet side
lcd_trim_btm  = 1.4;            // second print: window edge moved in on the button side
lcd_bevel     = 1.2;            // width of the bevel on the panel face, over the lip depth
lcd_lip       = 0.8;            // front stop lip in front of the glass
lcd_gap       = 0.2;
lcd_clear     = 0.2;
lcd_lead_in   = 0.6;            // pocket walls flare by this much per side towards the entrance: the
                                // metal side tabs of the module caught the straight walls on the second print
glass_z       = box_H - lcd_lip - lcd_gap;   // 34.0
esp_post_top  = glass_z - esp_total_h;       // 25.05

/* [Buttons: 12x12 tact switch + factory cap, 12.0 mm total] */
btn_x          = [71.0, 88.0, 105.0];  // the right switch must clear the corner screw post
btn_y          = 16.0;
btn_labels     = ["POWER", "EFFECT", "SPEED"];
btn_label_ink  = ["white", "blue", "white"];  // colour inlay of each label
// Synthetic narrow bold: Liberation Sans Bold squeezed in X, outline grown, letters spaced
// apart. First print: 3.2 mm text had 0.53 mm strokes and the slicer dropped parts of the
// first-layer inlay; now strokes ~0.70 mm, 0.5 mm between letters, labels fit the 17 mm pitch.
btn_label_size = 3.4;
btn_label_xs   = 0.72;
btn_label_grow = 0.12;
btn_label_sp   = 1.25;
btn_pocket     = [12.2, 13.0];  // X, Y: the switch legs sit on the front and back sides
btn_pocket_d   = 3.0;
btn_total_h    = 12.0;
btn_protrude   = 1.75;   // nominal cap top above the faceplate
btn_seat_lower = 2.0;    // first print: the lid did not close with the real switch + cap
btn_hole_d     = 12.2;   // for the D11.5 cap head
btn_pedestal   = [15.0, 16.0];
btn_rib_w      = 5.0;    // single support rib across the middle (along X)
btn_leg_well   = 6.0;    // depth of the leg / solder wells below the seat
btn_seat_z     = box_H + btn_protrude - btn_total_h - btn_seat_lower;  // 22.75

/* [MOSFET module and filter capacitor (DC bay)] */
mosfet_pcb   = [6.0, 13.2, 1.0];  // X, Y, PCB thickness (2.0 with parts)
mosfet_clear = 0.3;
mosfet_x0    = 66.0;
mosfet_y0    = 30.0;
cap_d        = 8.0;   // Ø8 x 12 size; the 100 uF / 50 V part (8.2 x 12.3 with sleeve) fits as printed
cap_len      = 12.0;
cap_cx       = 69.5;
cap_y0       = 48.0;

/* [JST-XH-3A garland outlet (right wall)] */
jst_window = [10.2, 6.0];   // flush header window (Y, Z)
jst_depth  = 7.0;           // header length from the outer wall face
jst_cy     = 43.0;
jst_z0     = 8.0;
jst_post_h = 16.4;          // lid post below the plate, set by the fit check

/* [Assembly screws: posts on the lid with M3 heat-set inserts, screwed from the bottom] */
boss_r          = 4.2;   // lid post radius
boss_inset      = 7.0;   // post centre from the outer edges
post_clear      = 0.25;  // base features keep this distance from the posts
post_gap        = 0.1;   // post tip to base pad
pad_r           = 5.0;   // base pad under each post
pad_top         = 3.5;   // floor 2.5 + 1.0 mm pad
insert_d        = 4.0;   // hole for an M3 x 5.7 insert (OD 4.6); adjust for other inserts
insert_len      = 5.7;
insert_depth    = insert_len + 0.8;
screw_relief_d  = 3.2;   // above the insert: screw tip and displaced melt
screw_relief_h  = 2.0;   // short, so the post is solid right above the insert
screw_clear_r   = 1.7;   // base pad: M3 clearance hole
screw_cbore_d   = 6.0;   // counterbore for an M3 button-head hex screw (head D5.4 x 2.0)
screw_cbore_h   = 2.0;   // head flush with the bottom; M3 x 6 engages 4.5 mm (M3 x 5: 3.5 mm)
post_bottom_z   = pad_top + post_gap;  // post tip height in base coordinates
post_root_h     = 6.0;   // flared post root under the lid plate; stays above the button pedestals
post_root_r     = 3.0;

/* [Labels] */
label_font  = "Liberation Sans:style=Bold";
label_depth = 0.6;   // engraved on the lid (printed face down), raised on the walls
inlay_depth = 0.4;   // colour inlays: exactly two 0.2 mm layers

warning_y   = 12.0;  // engraved "220V~ INSIDE" (no colour), front of the AC bay

/* [Colour inlays on the lid: logo "csi" + POWER + SPEED white, "Garland" + EFFECT sky blue] */
// Logo in the csiPass 5x7 pixel face (csiPass firmware ui_text.cpp, glyphFor):
// five column bytes per glyph, bit 0 = top row, 6-cell pitch like the csiPass wordmark.
logo_text   = "csiGarland";
logo_split  = 3;                     // first three glyphs white, the rest sky blue
logo_pixel  = 0.85;                  // cell size: two 0.4 mm nozzle lines per pixel
logo_x      = partition_x / 2;       // centred over the AC bay
logo_top    = 50.5;
logo_cells  = len(logo_text) * 6 - 1;
logo_left   = logo_x - logo_cells * logo_pixel / 2;
pixel_glyphs = [
    ["c", [56, 68, 68, 68, 32]],
    ["s", [72, 84, 84, 84, 32]],
    ["i", [0, 68, 125, 64, 0]],
    ["G", [62, 65, 73, 73, 122]],
    ["a", [32, 84, 84, 84, 120]],
    ["r", [124, 8, 4, 4, 8]],
    ["l", [0, 65, 127, 64, 0]],
    ["n", [124, 8, 4, 4, 120]],
    ["d", [56, 68, 68, 72, 127]],
];

/* [View] */
view_mode = "all";  // [all, assembled, base, lid, inlay_white, inlay_blue, test_xt60, test_jst, test_lcd]

// =============================================================================
// HELPERS
// =============================================================================

module rounded_box(l, w, h, r) {
    translate([r, r, 0])
        linear_extrude(h)
            offset(r = r) square([l - 2 * r, w - 2 * r]);
}

// Axis-aligned box between two corners given in any order.
module span(xa, xb, ya, yb, za, zb) {
    translate([min(xa, xb), min(ya, yb), min(za, zb)])
        cube([abs(xb - xa), abs(yb - ya), abs(zb - za)]);
}

function boss_positions() = [for (x = [boss_inset, box_L - boss_inset], y = [boss_inset, box_W - boss_inset]) [x, y]];
function esp_holes() = [for (sx = [-1, 1], sy = [-1, 1]) [esp_cx + sx * esp_hole_dx / 2, esp_cy + sy * esp_hole_dy / 2]];

module label(txt, size) {
    text(txt, size = size, font = label_font, halign = "center", valign = "center");
}

function pixel_glyph(ch) = pixel_glyphs[search([ch], pixel_glyphs)[0]][1];

// Glyphs [from, to) of logo_text as pixel squares. Each square grows by 0.01 mm so
// pixels that only touch at a corner still merge into one solid letter.
module logo_pixels_2d(from, to) {
    for (i = [from : to - 1], col = [0 : 4], row = [0 : 6])
        if (floor(pixel_glyph(logo_text[i])[col] / pow(2, row)) % 2 == 1)
            translate([logo_left + (i * 6 + col) * logo_pixel - 0.01, logo_top - (row + 1) * logo_pixel - 0.01])
                square(logo_pixel + 0.02);
}

module logo_csi_2d() {
    logo_pixels_2d(0, logo_split);
}

module logo_garland_2d() {
    logo_pixels_2d(logo_split, len(logo_text));
}

module button_labels_2d(ink) {
    for (i = [0 : 2])
        if (btn_label_ink[i] == ink)
            translate([btn_x[i], btn_y + 10.4])
                offset(delta = btn_label_grow) scale([btn_label_xs, 1])
                    text(btn_labels[i], size = btn_label_size, font = label_font, spacing = btn_label_sp,
                         halign = "center", valign = "center");
}

module inlay_2d(ink) {
    if (ink == "white") logo_csi_2d();
    else logo_garland_2d();
    button_labels_2d(ink);
}

// Flush colour inlay ("white" or "blue") filling its lid recess, in lid coordinates.
// The lid prints face down, so the inlays only occupy the first two layers.
module panel_inlay(ink) {
    translate([0, 0, lid_t - inlay_depth])
        linear_extrude(inlay_depth) inlay_2d(ink);
}

// Raised label on the outer face of the left (side = -1) or right (side = +1) wall.
module wall_label(txt, size, y, z, side) {
    translate([side < 0 ? 0.01 : box_L - 0.01, y, z])
        rotate([0, 0, side < 0 ? -90 : 90])
            rotate([90, 0, 0])
                linear_extrude(label_depth + 0.01) label(txt, size);
}

// =============================================================================
// 1. BASE TUB
// =============================================================================

module base_shell() {
    difference() {
        rounded_box(box_L, box_W, base_H, corner_r);
        span(wall_t, partition_x, wall_t, box_W - wall_t, floor_t, base_H + 1);  // AC bay
        span(dc_x0, box_L - wall_t, wall_t, box_W - wall_t, floor_t, base_H + 1);  // DC bay
    }
}

module hilink_supports() {
    for (cx = [0, 1], cy = [0, 1]) {
        x  = hilink_x0 + cx * hilink_bay[0];
        y  = hilink_y0 + cy * hilink_bay[1];
        ox = cx ? 1 : -1;
        oy = cy ? 1 : -1;
        // Corner shoulder under the module (leaves the 4 mm pin plenum open)
        span(x, x - ox * hilink_shoulder, y, y - oy * hilink_shoulder, floor_t - 0.01, floor_t + hilink_standoff);
        // L-shaped guide pillar outside the corner
        span(x, x + ox * cradle_t, y - oy * hilink_guide_len, y + oy * cradle_t, floor_t - 0.01, floor_t + hilink_guide_h);
        span(x - ox * hilink_guide_len, x + ox * cradle_t, y, y + oy * cradle_t, floor_t - 0.01, floor_t + hilink_guide_h);
    }
}

module xt60_sleeve() {
    w   = xt60_pocket[0] + 2 * cradle_t;
    top = xt60_z0 + xt60_pocket[1];
    span(wall_t - 0.01, wall_t + xt60_L, xt60_cy - w / 2, xt60_cy + w / 2, floor_t - 0.01, top);
    // Backstop behind the sheath absorbs the plug insertion force
    span(wall_t + xt60_L, wall_t + xt60_L + 2.0, xt60_cy - w / 2, xt60_cy + w / 2, floor_t - 0.01, top);
}

module xt60_cuts() {
    wz = xt60_z0 + (xt60_pocket[1] - xt60_window[1]) / 2;
    span(-0.1, wall_t + 0.01, xt60_cy - xt60_window[0] / 2, xt60_cy + xt60_window[0] / 2, wz, wz + xt60_window[1]);
    span(wall_t, wall_t + xt60_L, xt60_cy - xt60_pocket[0] / 2, xt60_cy + xt60_pocket[0] / 2, xt60_z0, base_H + 1);
    span(wall_t + xt60_L - 0.1, wall_t + xt60_L + 2.1, xt60_cy - 5.0, xt60_cy + 5.0, xt60_z0 + 1.5, base_H + 1);
}

module fuse_cradle() {
    y1 = fuse_y0 + fuse_holder[1];
    x1 = fuse_x0 + fuse_holder[0];
    span(fuse_x0, x1, fuse_y0 - cradle_t, fuse_y0, floor_t - 0.01, floor_t + fuse_cradle_h);
    span(fuse_x0, x1, y1, y1 + cradle_t, floor_t - 0.01, floor_t + fuse_cradle_h);
    // Friction lips at the top edges (open ends for the leads)
    span(fuse_x0 + 3, x1 - 3, fuse_y0, fuse_y0 + 0.4, floor_t + fuse_cradle_h - 1.0, floor_t + fuse_cradle_h);
    span(fuse_x0 + 3, x1 - 3, y1 - 0.4, y1, floor_t + fuse_cradle_h - 1.0, floor_t + fuse_cradle_h);
}

module mov_cradle() {
    difference() {
        span(mov_x0 - cradle_t, mov_x0 + mov_slot[0] + cradle_t, mov_y0 - cradle_t, mov_y0 + mov_slot[1] + cradle_t,
             floor_t - 0.01, floor_t + mov_cradle_h);
        span(mov_x0, mov_x0 + mov_slot[0], mov_y0, mov_y0 + mov_slot[1], floor_t, floor_t + mov_cradle_h + 1);
    }
}

module esp_post() {
    body_h = esp_post_top - floor_t - esp_post_slim - esp_post_cone;
    translate([0, 0, floor_t - 0.01]) {
        cylinder(d = esp_post_d, h = esp_post_top - floor_t + 0.01);
        // Reinforcement: thick lower body tapering into the slim top under the board,
        // gussets along Y (the capacitor and MOSFET cradles sit next to the posts in X)
        cylinder(d = esp_post_base_d, h = body_h);
        translate([0, 0, body_h]) cylinder(d1 = esp_post_base_d, d2 = esp_post_d, h = esp_post_cone);
        rotate([90, 0, 90])
            linear_extrude(esp_rib_t, center = true)
                polygon([[-esp_rib_reach, 0], [esp_rib_reach, 0],
                         [esp_post_base_d / 2, esp_rib_h], [-esp_post_base_d / 2, esp_rib_h]]);
    }
}

module esp_post_cut() {
    translate([0, 0, -0.1]) cylinder(d = esp_bore_d, h = esp_post_top - esp_web + 0.1);
    cylinder(d = esp_screw_d, h = esp_post_top + 1);
}

module esp_posts() {
    for (p = esp_holes())
        translate([p[0], p[1], 0]) esp_post();
}

module esp_post_cuts() {
    for (p = esp_holes())
        translate([p[0], p[1], 0]) esp_post_cut();
}

module button_pedestals() {
    for (x = btn_x)
        span(x - btn_pedestal[0] / 2, x + btn_pedestal[0] / 2, btn_y - btn_pedestal[1] / 2, btn_y + btn_pedestal[1] / 2,
             floor_t - 0.01, btn_seat_z + btn_pocket_d);
}

module button_cuts() {
    for (x = btn_x) {
        // Switch guide well, 13 mm deep in Y for the legs on the front and back sides
        span(x - btn_pocket[0] / 2, x + btn_pocket[0] / 2, btn_y - btn_pocket[1] / 2, btn_y + btn_pocket[1] / 2,
             btn_seat_z, btn_seat_z + btn_pocket_d + 0.1);
        // Leg / solder wells on both sides of the single middle rib, open through the
        // front and back faces of the pedestal for the flying leads
        for (s = [-1, 1])
            span(x - btn_pocket[0] / 2, x + btn_pocket[0] / 2,
                 btn_y + s * btn_rib_w / 2, btn_y + s * (btn_pedestal[1] / 2 + 0.1),
                 btn_seat_z - btn_leg_well, btn_seat_z + 0.01);
    }
}

module mosfet_cradle() {
    ix = mosfet_pcb[0] + 2 * mosfet_clear;
    iy = mosfet_pcb[1] + 2 * mosfet_clear;
    lz = floor_t + mosfet_pcb[2] + 0.1;  // latch underside just above the PCB
    for (s = [0, 1]) {
        xw = s ? mosfet_x0 + ix : mosfet_x0 - 1.2;
        span(xw, xw + 1.2, mosfet_y0 + 2.0, mosfet_y0 + iy - 2.0, floor_t - 0.01, lz + 0.8);
        xl = s ? mosfet_x0 + ix - 0.6 : mosfet_x0;  // 0.3 mm catch over each PCB edge
        span(xl, xl + 0.6, mosfet_y0 + 4.0, mosfet_y0 + iy - 4.0, lz, lz + 0.8);
    }
    // End stops at the corners; the middle of both ends stays open for the pin wires
    for (sx = [0, 1], sy = [0, 1]) {
        x = sx ? mosfet_x0 + ix - 1.2 : mosfet_x0;
        y = sy ? mosfet_y0 + iy : mosfet_y0 - 1.2;
        span(x, x + 1.2, y, y + 1.2, floor_t - 0.01, lz);
    }
}

module cap_saddle() {
    span(cap_cx - 5.0, cap_cx + 5.0, cap_y0, cap_y0 + cap_len, floor_t - 0.01, floor_t + 0.6 + cap_d / 2 + 1.2);
}

// Applied to the whole base: the nearest ESP32 post also must not enter the capacitor channel
module cap_cut() {
    // Snap opening at the top is narrower than the capacitor (7.84 < 8.2 mm)
    translate([cap_cx, cap_y0 - 0.1, floor_t + 0.6 + cap_d / 2])
        rotate([-90, 0, 0]) cylinder(d = cap_d + 0.2, h = cap_len + 0.2);
}

module jst_sleeve() {
    x0  = box_L - jst_depth;
    w   = jst_window[0] + 2 * cradle_t;
    top = jst_z0 + jst_window[1] + cradle_t;
    span(x0, box_L - wall_t + 0.01, jst_cy - w / 2, jst_cy + w / 2, floor_t - 0.01, top);
    span(x0 - 2.0, x0, jst_cy - w / 2, jst_cy + w / 2, floor_t - 0.01, top);  // backstop
}

module jst_cuts() {
    x0 = box_L - jst_depth;
    span(x0, box_L + 0.1, jst_cy - jst_window[0] / 2, jst_cy + jst_window[0] / 2, jst_z0, jst_z0 + jst_window[1]);
    span(x0, box_L - wall_t, jst_cy - jst_window[0] / 2, jst_cy + jst_window[0] / 2, jst_z0, base_H + 1);
    span(x0 - 2.1, x0 + 0.1, jst_cy - 4.5, jst_cy + 4.5, jst_z0 + 1.0, jst_z0 + jst_window[1] - 1.0);
}

module enclosure_base() {
    difference() {
        union() {
            base_shell();
            // Pads under the lid posts (screws go in from the bottom)
            for (p = boss_positions())
                translate([p[0], p[1], 0]) cylinder(r = pad_r, h = pad_top);
            hilink_supports();
            xt60_sleeve();
            fuse_cradle();
            mov_cradle();
            esp_posts();
            button_pedestals();
            mosfet_cradle();
            cap_saddle();
            jst_sleeve();
            wall_label("AC 220V~", 4.0, xt60_cy, xt60_z0 + xt60_pocket[1] + 5.0, -1);
            wall_label("LED OUT", 4.0, jst_cy, jst_z0 + jst_window[1] + 5.0, 1);
        }

        // Screw holes from the bottom: clearance + flush counterbore for the button head;
        // everything else keeps clear of the lid posts above the pads
        for (p = boss_positions())
            translate([p[0], p[1], 0]) {
                translate([0, 0, -0.1]) cylinder(r = screw_clear_r, h = pad_top + 0.2);
                translate([0, 0, -0.1]) cylinder(d = screw_cbore_d, h = screw_cbore_h + 0.1);
                translate([0, 0, pad_top]) cylinder(r = boss_r + post_clear, h = base_H);
            }

        // Hi-Link bay above the corner shoulders
        span(hilink_x0, hilink_x0 + hilink_bay[0], hilink_y0, hilink_y0 + hilink_bay[1],
             floor_t + hilink_standoff, base_H + 1);

        xt60_cuts();
        jst_cuts();

        // DC wire pass-through at the bottom of the partition, next to the Hi-Link DC pins
        span(partition_x - 0.1, dc_x0 + 0.1, hilink_y0 + 8.0, hilink_y0 + 20.0, floor_t, floor_t + 6.0);

        esp_post_cuts();
        button_cuts();
        cap_cut();

        // Rubber feet recesses (4x D8 x 1.0), clear of the screw counterbores
        for (fx = [16.0, box_L - 16.0], fy = [14.0, box_W - 14.0])
            translate([fx, fy, -0.5]) cylinder(r = 4.0, h = 1.5);
    }
}

// =============================================================================
// 2. LID (control panel). Local Z = 0 is the underside of the plate.
// =============================================================================

module lid_lip() {
    o = wall_t + lip_clear;
    difference() {
        span(o, box_L - o, o, box_W - o, -lip_h, 0.01);
        span(o + lip_w, box_L - o - lip_w, o + lip_w, box_W - o - lip_w, -lip_h - 1, 1);
        span(partition_x - 0.3, dc_x0 + 0.3, 0, box_W, -lip_h - 1, 0.02);
    }
}

module enclosure_lid() {
    difference() {
        union() {
            rounded_box(box_L, box_W, lid_t, corner_r);
            lid_lip();
            // Hi-Link retaining ribs (narrow so they crush instead of bowing the lid)
            for (x = [hilink_x0 + 10.0, hilink_x0 + hilink_bay[0] - 10.0])
                span(x - 0.8, x + 0.8, hilink_y0 + 3.0, hilink_y0 + hilink_bay[1] - 3.0,
                     -hilink_rib_h, 0.01);
            // Hold-down posts keep the XT60 and JST bodies in their open-top sleeves
            span(wall_t + 6.0, wall_t + 12.0, xt60_cy - 3.0, xt60_cy + 3.0,
                 -xt60_post_h, 0.01);
            span(box_L - jst_depth + 0.5, box_L - wall_t - 0.5, jst_cy - 2.5, jst_cy + 2.5,
                 -jst_post_h, 0.01);
            // Corner posts down to the base pads, M3 heat-set inserts in their tips; a flared
            // root (+3 mm over 6 mm, kept inside the tub and above the Hi-Link) stiffens the joint with the plate
            for (p = boss_positions())
                translate([p[0], p[1], post_bottom_z - base_H]) cylinder(r = boss_r, h = base_H - post_bottom_z + 0.01);
            intersection() {
                difference() {
                    span(wall_t + lip_clear, box_L - wall_t - lip_clear, wall_t + lip_clear, box_W - wall_t - lip_clear, -post_root_h, 0.01);
                    span(hilink_x0, hilink_x0 + hilink_bay[0], hilink_y0, hilink_y0 + hilink_bay[1], -post_root_h - 1, -hilink_rib_h);
                }
                for (p = boss_positions())
                    translate([p[0], p[1], -post_root_h]) cylinder(r1 = boss_r, r2 = boss_r + post_root_r, h = post_root_h + 0.01);
            }
        }

        for (p = boss_positions())
            translate([p[0], p[1], post_bottom_z - base_H]) {
                translate([0, 0, -0.1]) cylinder(d = insert_d, h = insert_depth + 0.1);
                translate([0, 0, -0.01]) cylinder(d1 = insert_d + 0.8, d2 = insert_d, h = 0.41);
                translate([0, 0, insert_depth - 0.01]) cylinder(d = screw_relief_d, h = screw_relief_h);
            }

        // LCD module pocket from below: nominal size at the lip (fits the module as printed),
        // walls flaring towards the entrance like a funnel; then the front aperture narrowing
        // towards the glass (bevel), offset to the active area
        pkt_x = lcd_module[0] / 2 + lcd_clear;
        pkt_y = lcd_module[1] / 2 + lcd_clear;
        hull() {
            span(esp_cx - pkt_x, esp_cx + pkt_x, esp_cy - pkt_y, esp_cy + pkt_y, lid_t - lcd_lip - 0.01, lid_t - lcd_lip);
            span(esp_cx - pkt_x - lcd_lead_in, esp_cx + pkt_x + lcd_lead_in,
                 esp_cy - pkt_y - lcd_lead_in, esp_cy + pkt_y + lcd_lead_in, -0.01, 0);
        }
        span(esp_cx - pkt_x - lcd_lead_in, esp_cx + pkt_x + lcd_lead_in,
             esp_cy - pkt_y - lcd_lead_in, esp_cy + pkt_y + lcd_lead_in, -1, 0);
        win_cx = esp_cx + lcd_active_dx;
        win_cy = esp_cy + lcd_active_dy;
        win_x0 = win_cx - lcd_aperture / 2 + lcd_trim_left;
        win_x1 = win_cx + lcd_aperture / 2;
        win_y0 = win_cy - lcd_aperture / 2 + lcd_trim_btm;
        win_y1 = win_cy + lcd_aperture / 2;
        hull() {
            span(win_x0, win_x1, win_y0, win_y1, lid_t - lcd_lip - 0.01, lid_t - lcd_lip);
            span(win_x0 - lcd_bevel, win_x1 + lcd_bevel, win_y0 - lcd_bevel, win_y1 + lcd_bevel, lid_t, lid_t + 0.01);
        }

        for (x = btn_x) {
            translate([x, btn_y, -1]) cylinder(d = btn_hole_d, h = lid_t + 2);
            translate([x, btn_y, lid_t - 0.5]) cylinder(d1 = btn_hole_d, d2 = btn_hole_d + 1.2, h = 0.51);
        }

        // Recesses for the colour inlays, plus the plain engraved mains warning
        translate([0, 0, lid_t - inlay_depth])
            linear_extrude(inlay_depth + 1) {
                inlay_2d("white");
                inlay_2d("blue");
            }
        translate([0, 0, lid_t - label_depth])
            linear_extrude(label_depth + 1)
                translate([partition_x / 2, warning_y]) label("220V~ INSIDE", 3.2);
    }
}

// =============================================================================
// 3. COMPONENT MOCKUPS (preview only)
// =============================================================================

module components_mockup() {
    color("#3A3A3A") translate([hilink_x0 + hilink_gap, hilink_y0 + hilink_gap, floor_t + hilink_standoff])
        cube([hilink_L, hilink_W, hilink_H]);
    color("#F2C200") translate([wall_t, xt60_cy - 8.2, xt60_z0 + 0.1]) cube([xt60_L, 16.4, 9.2]);
    color("#1A1A1A") translate([esp_cx - esp_pcb[0] / 2, esp_cy - esp_pcb[1] / 2, esp_post_top])
        cube([esp_pcb[0], esp_pcb[1], esp_total_h]);
    color("#FFFFFF") translate([box_L - jst_depth, jst_cy - 4.95, jst_z0 + 0.1]) cube([jst_depth, 9.9, 5.75]);
    for (i = [0 : 2])
        color(["#D90429", "#EDF2F4", "#3A86FF"][i]) translate([btn_x[i], btn_y, btn_seat_z]) {
            translate([-6, -6, 0]) cube([12, 12, 3.5]);
            cylinder(d = 11.5, h = btn_total_h);
        }
}

// =============================================================================
// RENDER DISPATCHER
// =============================================================================

if (view_mode == "base") {
    enclosure_base();
} else if (view_mode == "lid") {
    enclosure_lid();
} else if (view_mode == "inlay_white") {
    panel_inlay("white");
} else if (view_mode == "inlay_blue") {
    panel_inlay("blue");
} else if (view_mode == "test_xt60") {
    // Small coupon: print it first to check the XT60H-F fit and plug mating
    intersection() {
        enclosure_base();
        span(-1, wall_t + xt60_L + 4.0, xt60_cy - 13.0, xt60_cy + 13.0, -1, xt60_z0 + xt60_pocket[1] + 2.0);
    }
} else if (view_mode == "test_jst") {
    intersection() {
        enclosure_base();
        span(box_L - jst_depth - 4.0, box_L + 1, jst_cy - 9.0, jst_cy + 9.0, -1, jst_z0 + jst_window[1] + 3.0);
    }
} else if (view_mode == "test_lcd") {
    // Lid fragment around the display window, to check the LCD module fit
    intersection() {
        enclosure_lid();
        span(esp_cx - 18.0, esp_cx + 18.0, esp_cy - 19.0, esp_cy + 19.0, -lip_h - 1, lid_t + 1);
    }
} else if (view_mode == "assembled") {
    color("#2B2D42") enclosure_base();
    components_mockup();
    translate([0, 0, base_H]) {
        color("#2B2D42", 0.8) enclosure_lid();
        color("#FFFFFF") panel_inlay("white");
        color("#5BC0EB") panel_inlay("blue");
    }
} else {
    color("#2B2D42") enclosure_base();
    translate([0, 0, base_H + 18.0]) {
        color("#2B2D42") enclosure_lid();
        color("#FFFFFF") panel_inlay("white");
        color("#5BC0EB") panel_inlay("blue");
    }
}
