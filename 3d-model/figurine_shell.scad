/**
 * Copyright (c) 2026 Anton Bespalov. All rights reserved.
 * Licensed under the terms specified by the MIT License
 * Part of the csiGarland project https://github.com/ABespalov/csigarland
 *
 * Purpose: Even-walled shells for the figurines: a solid given by its radius around the
 *          Z axis, r = R(a, z), and the same solid eroded by a wall thickness, so that a
 *          cap cut from the one by the other glows evenly over its whole surface.
 * Contributors: Anton Bespalov
 */

// The radius of the solid eroded by w, at angle a and height z. In the meridian
// plane at a, a point lies inside the eroded solid when the disc of radius w
// around it lies inside the solid, so the eroded radius is the smallest of
// R(a, z') - sqrt(w^2 - (z' - z)^2) over |z' - z| <= w; w grows with the slope
// across the angle, where the wall would otherwise thin on the flanks of lobes.
function shell_eroded(R, a, z, w, n = 6) =
    let (r = R(a, z),
         mt = (R(a + 0.5, z) - R(a - 0.5, z)) / (PI / 180) / max(r, 0.01),
         W = w * sqrt(1 + mt * mt))
    min([for (k = [-n : n]) let (dz = W * k / n) R(a, z + dz) - sqrt(max(0, W * W - dz * dz))]);

// Radii of the rings from z0 to z1, eroded by w when w > 0. The caps print collar down,
// so a hollow's ceiling is where its radius shrinks going up. With ceiling = angle, it
// shrinks no faster than that angle from the horizontal allows, f(z) = min over z' >= z
// of r(z') + (z' - z) / tan(angle): a flatter ceiling would take support inside the
// closed hollow, where it could never be taken out. The wall is thicker there instead.
function shell_rings(R, z0, z1, m, na, w, ceiling) =
    let (rings = [for (j = [0 : m]) let (z = z0 + (z1 - z0) * j / m)
                     [for (i = [0 : na - 1]) let (a = i * 360 / na)
                         w > 0 ? shell_eroded(R, a, z, w) : R(a, z)]],
         step = (z1 - z0) / m,
         k = ceiling > 0 ? step / tan(ceiling) : 0,
         reach = ceiling > 0 ? ceil(25 / k) : 0)
    ceiling > 0
        ? [for (j = [0 : m]) [for (i = [0 : na - 1])
              min([for (q = [j : min(m, j + reach)]) rings[q][i] + (q - j) * k])]]
        : rings;

// Points of the rings, and the two axis points that close the ends.
function shell_points(R, z0, z1, m, na, w, ceiling) =
    let (rings = shell_rings(R, z0, z1, m, na, w, ceiling))
    concat(
        [for (j = [0 : m]) let (z = z0 + (z1 - z0) * j / m)
            for (i = [0 : na - 1]) let (a = i * 360 / na, r = max(0.05, rings[j][i]))
                [r * cos(a), r * sin(a), z]],
        [[0, 0, z0], [0, 0, z1]]);

// The solid r <= R(a, z) for z0 <= z <= z1, eroded by w when w > 0, its ceilings held
// to ceiling degrees from the horizontal when ceiling > 0.
module shell_solid(R, z0, z1, dz = 0.25, na = 72, w = 0, ceiling = 0) {
    m = ceil((z1 - z0) / dz);
    bottom = (m + 1) * na;
    top = bottom + 1;
    polyhedron(
        points = shell_points(R, z0, z1, m, na, w, ceiling),
        faces = concat(
            [for (j = [0 : m - 1]) for (i = [0 : na - 1])
                let (p = j * na + i, q = j * na + (i + 1) % na)
                    for (f = [[p, p + na, q + na], [p, q + na, q]]) f],
            [for (i = [0 : na - 1]) [bottom, i, (i + 1) % na]],
            [for (i = [0 : na - 1]) [top, m * na + (i + 1) % na, m * na + i]]),
        convexity = 10);
}

// ---------------------------------------------------------------------------
// Hollows of solids built from spheres, ellipsoids and frustums. Each primitive is
// given already eroded by the wall, and a ray from the axis at angle a and height z
// gathers the stretches it runs inside them; the hollow's radius is where that run,
// unbroken from the axis, ends. Inside its own primitive every point of it lies a
// wall or more from that primitive's surface, so the hollow keeps a full wall to the
// outside of the union, and the rays never jump a gap to a part of it the axis does
// not reach. shell_solid() of shell_ray_union(), with a ceiling angle, is the hollow.
//
//   ["e", c, A]                  ellipsoid: |A (p - c)| <= 1
//   ["f", z0, z1, c0, c1, r0, r1] the hull of two horizontal discs: at z a circle whose
//                                centre [x, y] and radius go from c0, r0 to c1, r1
//   ["p", z0, z1, r0, r1, n, rot] a regular n-gon frustum round the axis, circumradius
//                                r0 to r1, a corner at angle rot
//   ["t", zc, rc, ar, az]        a ring round the axis whose section is an ellipse of
//                                half-axes ar (across) and az (up) centred at radius rc
//   ["x", z0, z1, a0, b0, a1, b1] an elliptic frustum round the axis, half-axes along X
//                                and Y going from a0, b0 to a1, b1
// ---------------------------------------------------------------------------

// A sphere of radius r at c, as an ellipsoid; nothing when r is too small to matter.
function shell_sphere(c, r) = r > 0.2 ? [["e", c, [[1 / r, 0, 0], [0, 1 / r, 0], [0, 0, 1 / r]]]] : [];

// An ellipsoid of half-axes s, turned by rotate(rot), at c.
function shell_rot(rot) =
    let (x = rot[0], y = rot[1], z = rot[2],
         Rx = [[1, 0, 0], [0, cos(x), -sin(x)], [0, sin(x), cos(x)]],
         Ry = [[cos(y), 0, sin(y)], [0, 1, 0], [-sin(y), 0, cos(y)]],
         Rz = [[cos(z), -sin(z), 0], [sin(z), cos(z), 0], [0, 0, 1]])
    Rz * Ry * Rx;
function shell_ellipsoid(c, s, rot = [0, 0, 0]) =
    min(s) > 0.2 ? [["e", c, [[1 / s[0], 0, 0], [0, 1 / s[1], 0], [0, 0, 1 / s[2]]] * transpose3(shell_rot(rot))]] : [];
function transpose3(m) = [[m[0][0], m[1][0], m[2][0]], [m[0][1], m[1][1], m[2][1]], [m[0][2], m[1][2], m[2][2]]];

// The hull of two spheres, as a chain of n + 1 spheres between them.
function shell_capsule(p0, r0, p1, r1, n = 8) =
    [for (i = [0 : n]) let (t = i / n) each shell_sphere(p0 + (p1 - p0) * t, r0 + (r1 - r0) * t)];

// The stretch [in, out] of the ray inside one primitive, or [] when it misses.
function shell_ray_hit(p, a, z) =
    let (u = [cos(a), sin(a), 0])
    p[0] == "e" ? (
        let (o = p[2] * ([0, 0, z] - p[1]), d = p[2] * u,
             A = d * d, B = 2 * (o * d), C = o * o - 1, D = B * B - 4 * A * C)
        D <= 0 ? [] : let (s = sqrt(D), t1 = (-B - s) / (2 * A), t2 = (-B + s) / (2 * A))
            t2 <= 0 ? [] : [t1, t2]
    ) : p[0] == "f" ? (
        z < p[1] || z > p[2] ? [] :
        let (t = (z - p[1]) / (p[2] - p[1]), c = p[3] + (p[4] - p[3]) * t, r = p[5] + (p[6] - p[5]) * t,
             b = -(c[0] * u[0] + c[1] * u[1]), C = c * c - r * r, D = b * b - C)
        r <= 0 || D <= 0 ? [] : let (s = sqrt(D)) (-b + s) <= 0 ? [] : [-b - s, -b + s]
    ) : p[0] == "p" ? (
        z < p[1] || z > p[2] ? [] :
        let (r = p[3] + (p[4] - p[3]) * (z - p[1]) / (p[2] - p[1]), n = p[5],
             e = 360 / n, f = ((a - p[6]) % e + e) % e - e / 2)
        r <= 0 ? [] : [-1, r * cos(e / 2) / cos(f)]
    ) : p[0] == "x" ? (
        z < p[1] || z > p[2] ? [] :
        let (t = (z - p[1]) / (p[2] - p[1]), A = p[3] + (p[5] - p[3]) * t, B = p[4] + (p[6] - p[4]) * t)
        A <= 0 || B <= 0 ? [] : [-1, 1 / sqrt(pow(cos(a) / A, 2) + pow(sin(a) / B, 2))]
    ) : p[0] == "t" ? (
        let (dz = (z - p[1]) / p[4]) abs(dz) >= 1 ? [] :
        let (h = p[3] * sqrt(1 - dz * dz)) [p[2] - h, p[2] + h]
    ) : [];

function shell_ray_merge(iv, cur) =
    let (n = max(concat([cur], [for (v = iv) if (v[0] <= cur + 0.01 && v[1] > cur) v[1]])))
    n > cur + 1e-6 ? shell_ray_merge(iv, n) : cur;

// How far the ray from the axis at angle a and height z runs inside the primitives
// without a break; 0 when the axis itself lies outside them all.
function shell_ray_union(prims, a, z) =
    shell_ray_merge([for (p = prims) let (h = shell_ray_hit(p, a, z)) if (len(h) == 2) h], 0);