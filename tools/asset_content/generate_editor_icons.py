#!/usr/bin/env python3
# //////////////////////////////////////////////////////////////////////////
# //
# //  CypherEngine Source Code
# //  Copyright (c) 2026 Karlo Siric. All rights reserved.
# //
# //  File: tools/asset_content/generate_editor_icons.py
# //  Purpose: Generates the editor's colour icon set (Mason, the TileEditor,
# //           Picasso): tools, primitives, mesh and brush operations,
# //           selection, UVs and materials, terrain, entities, views, build,
# //           panels, assets, and console levels.
# //  Details: Version 2, on a 32 x 32 grid. Geometry is modelled in 3D (boxes,
# //           wedges, cylinders, cones, arches, stairs, grids), projected
# //           isometrically, back-face culled, depth sorted, and shaded from
# //           one light, so every shape has the same perspective and lighting
# //           and reads as the operation it names. Actions are drawn over the
# //           object in the SketchUp manner: red for doing (move, cut,
# //           extrude), green for creating, blue for selecting, orange for
# //           Hammer's selected elements. Flat glyphs (files, panels, console)
# //           use the same palette and outline.
# //
# //           The SVGs are the source the editor loads (crisp at any scale).
# //           PNGs at 24, 32, and 64 px and a contact sheet are exported with
# //           rsvg-convert and ImageMagick when present.
# //
# //           Original artwork; no third-party icons are copied.
# //
# //  History:
# //  - Created by Karlo Siric on 2026-09-30
# //  - 2026-09-30: version 2 - 32 px grid, 3D-modelled geometry, full set
# //  - 2026-10-01: refine core modelling silhouettes for the compact tool rail
# //  - 2026-10-02: Hammer 5 tone - modelled objects in dark steel with light
# //                edges, glyphs in light grey, every colour muted so only a
# //                small accent remains (Hammer's own icons, not a cartoon set)
# //
# //  This file is proprietary and confidential. See LICENSE for details.
# //
# //////////////////////////////////////////////////////////////////////////

import argparse
import colorsys
import math
import re
import shutil
import subprocess
from pathlib import Path

# ---------------------------------------------------------------------------
# Palette
# ---------------------------------------------------------------------------
OUT = "#1b1d20"          # Outline of flat glyphs.
WHITE = "#e4e7ea"
BOX = "#c4c9ce"          # Neutral glyphs: arrows, frames, cursors (Hammer's light grey).
BOX_DARK = "#9aa2aa"
# Hammer draws modelled objects as dark steel with light edges, not as white
# blocks with black outlines. Scene faces in BOX are drawn in STEEL instead.
STEEL = "#62788b"
STEEL_DARK = "#4a5a69"
EDGE = "#b3bbc3"         # Edges of modelled objects.
OBJECT_HUE = 0.5         # How much of a coloured object's own colour survives over steel.
TAN = "#d8b684"
RED = "#e2432f"          # Doing: move, cut, extrude, delete.
GREEN = "#3fae57"        # Creating and adding.
BLUE = "#3a8ee8"         # Selecting and surfaces.
ORANGE = "#d4893a"       # Hammer's selected elements: its one real accent, kept as is.
YELLOW = "#f7c843"       # Measuring and lights.
PURPLE = "#9a6ae2"
TEAL = "#22b5a5"
PINK = "#ee6aa6"
BROWN = "#a8733d"
SKY = "#7cc4f5"
AXIS = ("#e5443a", "#4cbf4a", "#3a86f0")

# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------

def rgb(color):
    return tuple(int(color[i:i + 2], 16) for i in (1, 3, 5))


def hexc(r, g, b):
    return "#{:02x}{:02x}{:02x}".format(*(max(0, min(255, int(round(v)))) for v in (r, g, b)))


def scale(color, f):
    return hexc(*(v * f for v in rgb(color)))


def mix(a, b, t):
    ra, rb = rgb(a), rgb(b)
    return hexc(*(ra[i] + (rb[i] - ra[i]) * t for i in range(3)))


# ---------------------------------------------------------------------------
# SVG document
# ---------------------------------------------------------------------------

class Icon:
    def __init__(self) -> None:
        self.defs: list[str] = []
        self.body: list[str] = []
        self.n = 0

    def gradient(self, top: str, bottom: str, x2: float = 0.0, y2: float = 1.0) -> str:
        self.n += 1
        gid = f"g{self.n}"
        self.defs.append(
            f'<linearGradient id="{gid}" x1="0" y1="0" x2="{x2}" y2="{y2}">'
            f'<stop offset="0" stop-color="{top}"/><stop offset="1" stop-color="{bottom}"/></linearGradient>')
        return f"url(#{gid})"

    def radial(self, inner: str, outer: str, cx: float = 0.35, cy: float = 0.3) -> str:
        self.n += 1
        gid = f"r{self.n}"
        self.defs.append(
            f'<radialGradient id="{gid}" cx="{cx}" cy="{cy}" r="0.75">'
            f'<stop offset="0" stop-color="{inner}"/><stop offset="1" stop-color="{outer}"/></radialGradient>')
        return f"url(#{gid})"

    def add(self, element: str) -> None:
        self.body.append(element)

    def svg(self) -> str:
        defs = f"<defs>{''.join(self.defs)}</defs>" if self.defs else ""
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">'
                + defs + "".join(self.body) + "</svg>\n")


def pts(points) -> str:
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in points)


def poly(i: Icon, points, fill: str, stroke: str = OUT, width: float = 1.0, opacity: float = 1.0) -> None:
    i.add(f'<polygon points="{pts(points)}" fill="{fill}" fill-opacity="{opacity}" stroke="{stroke}" '
          f'stroke-width="{width}" stroke-linejoin="round"/>')


def polyline(i: Icon, points, stroke: str, width: float = 1.4, dash: str = "") -> None:
    d = f' stroke-dasharray="{dash}"' if dash else ""
    i.add(f'<polyline points="{pts(points)}" fill="none" stroke="{stroke}" stroke-width="{width}" '
          f'stroke-linecap="round" stroke-linejoin="round"{d}/>')


def line(i: Icon, x1, y1, x2, y2, stroke: str, width: float = 1.4, dash: str = "", cap: str = "round") -> None:
    d = f' stroke-dasharray="{dash}"' if dash else ""
    i.add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{stroke}" '
          f'stroke-width="{width}" stroke-linecap="{cap}"{d}/>')


def path(i: Icon, d: str, fill: str = "none", stroke: str = OUT, width: float = 1.0, opacity: float = 1.0) -> None:
    i.add(f'<path d="{d}" fill="{fill}" fill-opacity="{opacity}" stroke="{stroke}" stroke-width="{width}" '
          f'stroke-linecap="round" stroke-linejoin="round"/>')


def circle(i: Icon, cx, cy, r, fill: str, stroke: str = OUT, width: float = 1.0) -> None:
    i.add(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r:.2f}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>')


def rect(i: Icon, x, y, w, h, fill: str, stroke: str = OUT, width: float = 1.0, radius: float = 1.5) -> None:
    i.add(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="{radius}" fill="{fill}" '
          f'stroke="{stroke}" stroke-width="{width}"/>')


def text(i: Icon, x, y, value: str, size: float, fill: str, weight: int = 800) -> None:
    i.add(f'<text x="{x:.2f}" y="{y:.2f}" font-family="Helvetica, Arial, sans-serif" font-size="{size}" '
          f'font-weight="{weight}" fill="{fill}" text-anchor="middle">{value}</text>')


def group(i: Icon, transform: str) -> None:
    i.add(f'<g transform="{transform}">')


def end(i: Icon) -> None:
    i.add("</g>")


# ---------------------------------------------------------------------------
# 3D modelling: isometric projection, culling, sorting, shading
# ---------------------------------------------------------------------------
C30 = math.cos(math.radians(30))
S30 = 0.5
LIGHT = (0.30, 0.62, 1.0)
_LN = math.sqrt(sum(v * v for v in LIGHT))
LIGHT = tuple(v / _LN for v in LIGHT)


class Scene:
    """Faces in world units; +z up, the viewer in the +x +y +z octant."""

    def __init__(self, cx: float = 16.0, cy: float = 17.0, unit: float = 1.0) -> None:
        self.cx, self.cy, self.unit = cx, cy, unit
        self.faces = []

    def p(self, x, y, z):
        return (self.cx + (x - y) * C30 * self.unit, self.cy + (x + y) * S30 * self.unit - z * self.unit)

    def face(self, points, color, outline=EDGE, width=1.0, opacity=1.0, flat=False):
        # Coloured objects keep only a hint of their hue over the steel, so
        # an icon reads as grey metal; the hue is a cue, not the subject.
        if color != ORANGE:
            color = {BOX: STEEL, BOX_DARK: STEEL_DARK}.get(color) or mix(STEEL, color, OBJECT_HUE)
        outline = EDGE if outline == OUT else outline
        self.faces.append((points, color, outline, width, opacity, flat))

    def render(self, i: Icon) -> None:
        drawn = []
        for points, color, outline, width, opacity, flat in self.faces:
            n = normal(points)
            if n[0] + n[1] + n[2] <= 1e-6:
                continue  # Faces away from the viewer.
            depth = sum(p[0] + p[1] + p[2] for p in points) / len(points)
            drawn.append((depth, points, color, outline, width, opacity, flat, n))
        drawn.sort(key=lambda f: f[0])
        for depth, points, color, outline, width, opacity, flat, n in drawn:
            lit = color if flat else shade(color, n)
            fill = i.gradient(scale(lit, 1.06), scale(lit, 0.93)) if not flat else lit
            poly(i, [self.p(*q) for q in points], fill, outline, width, opacity)


def normal(points):
    nx = ny = nz = 0.0
    for a, b in zip(points, points[1:] + points[:1]):
        nx += (a[1] - b[1]) * (a[2] + b[2])
        ny += (a[2] - b[2]) * (a[0] + b[0])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    return (nx / length, ny / length, nz / length)


def shade(color, n):
    d = max(0.0, n[0] * LIGHT[0] + n[1] * LIGHT[1] + n[2] * LIGHT[2])
    f = 0.56 + 0.52 * d
    if n[2] > 0.7:
        f *= 1.06
    return scale(color, f)


def box(s: Scene, x0, y0, z0, x1, y1, z1, color, **kw):
    v = lambda x, y, z: (x, y, z)
    s.face([v(x0, y0, z1), v(x1, y0, z1), v(x1, y1, z1), v(x0, y1, z1)], color, **kw)   # top  (+z)
    s.face([v(x0, y0, z0), v(x0, y1, z0), v(x1, y1, z0), v(x1, y0, z0)], color, **kw)   # bottom
    s.face([v(x1, y0, z0), v(x1, y1, z0), v(x1, y1, z1), v(x1, y0, z1)], color, **kw)   # +x
    s.face([v(x0, y0, z0), v(x0, y0, z1), v(x0, y1, z1), v(x0, y1, z0)], color, **kw)   # -x
    s.face([v(x0, y1, z0), v(x0, y1, z1), v(x1, y1, z1), v(x1, y1, z0)], color, **kw)   # +y
    s.face([v(x0, y0, z0), v(x1, y0, z0), v(x1, y0, z1), v(x0, y0, z1)], color, **kw)   # -y


def wedge(s: Scene, x0, y0, z0, x1, y1, z1, color, **kw):
    """High at y0, down to z0 at y1 (a ramp facing +y)."""
    s.face([(x0, y0, z1), (x1, y0, z1), (x1, y1, z0), (x0, y1, z0)], color, **kw)        # slope
    s.face([(x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)], color, **kw)        # bottom
    s.face([(x1, y0, z0), (x1, y1, z0), (x1, y0, z1)], color, **kw)                      # +x side
    s.face([(x0, y0, z0), (x0, y0, z1), (x0, y1, z0)], color, **kw)                      # -x side
    s.face([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], color, **kw)        # back


def cylinder(s: Scene, cx, cy, z0, z1, r, color, n=20, **kw):
    ring = [(cx + r * math.cos(2 * math.pi * k / n), cy + r * math.sin(2 * math.pi * k / n)) for k in range(n)]
    s.face([(x, y, z1) for x, y in ring], color, **kw)
    s.face([(x, y, z0) for x, y in reversed(ring)], color, **kw)
    for (ax, ay), (bx, by) in zip(ring, ring[1:] + ring[:1]):
        s.face([(ax, ay, z0), (bx, by, z0), (bx, by, z1), (ax, ay, z1)], color, width=0.0, **kw)


def cone(s: Scene, cx, cy, z0, z1, r, color, n=20, **kw):
    ring = [(cx + r * math.cos(2 * math.pi * k / n), cy + r * math.sin(2 * math.pi * k / n)) for k in range(n)]
    s.face([(x, y, z0) for x, y in reversed(ring)], color, **kw)
    for (ax, ay), (bx, by) in zip(ring, ring[1:] + ring[:1]):
        s.face([(ax, ay, z0), (bx, by, z0), (cx, cy, z1)], color, width=0.0, **kw)


def silhouette_cylinder(i: Icon, s: Scene, cx, cy, z0, z1, r):
    """Outline a cylinder drawn without per-segment strokes."""
    n = 40
    top = [s.p(cx + r * math.cos(2 * math.pi * k / n), cy + r * math.sin(2 * math.pi * k / n), z1) for k in range(n)]
    poly(i, top, "none", OUT, 1.0)
    left = s.p(cx - r * C30 / C30 * 0.7071, cy + r * 0.7071, z0)
    # Side edges at the silhouette tangents (screen-left and screen-right).
    for sign in (-1, 1):
        a = s.p(cx + sign * r * 0.7071, cy - sign * r * 0.7071, z1)
        b = s.p(cx + sign * r * 0.7071, cy - sign * r * 0.7071, z0)
        line(i, a[0], a[1], b[0], b[1], OUT, 1.0)
    bottom = [s.p(cx + r * math.cos(t), cy + r * math.sin(t), z0) for t in
              [math.radians(a) for a in range(-45, 136, 9)]]
    polyline(i, bottom, OUT, 1.0)


def arch(s: Scene, x0, x1, y0, y1, z0, z1, r, color, n=10, **kw):
    """A block with a semicircular opening through it along x."""
    cy = (y0 + y1) / 2
    arc = [(cy + r * math.cos(math.pi * k / n), z0 + r * math.sin(math.pi * k / n)) for k in range(n + 1)]
    for x, flip in ((x1, False), (x0, True)):
        outline = [(y0, z0), (y0, z1), (y1, z1), (y1, z0)] + [(yy, zz) for yy, zz in arc]
        facepts = [(x, yy, zz) for yy, zz in outline]
        s.face(list(reversed(facepts)) if not flip else facepts, color, **kw)
    s.face([(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], color, **kw)
    s.face([(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)], color, **kw)
    s.face([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], color, **kw)
    for (ay, az), (by, bz) in zip(arc, arc[1:]):
        s.face([(x0, ay, az), (x1, ay, az), (x1, by, bz), (x0, by, bz)], scale(color, 0.8), width=0.4, **kw)


def stairs(s: Scene, x0, x1, y0, depth, rise, steps, color, **kw):
    for k in range(steps):
        box(s, x0, y0 + k * depth, 0, x1, y0 + (k + 1) * depth, rise * (steps - k), color, **kw)


# ---------------------------------------------------------------------------
# 2D action marks: chunky SketchUp-style arrows, plus and minus badges
# ---------------------------------------------------------------------------

def arrow(i: Icon, x1, y1, x2, y2, color=RED, shaft=2.6, head=6.5, headw=6.0):
    a = math.atan2(y2 - y1, x2 - x1)
    ux, uy = math.cos(a), math.sin(a)
    px, py = -uy, ux
    bx, by = x2 - ux * head, y2 - uy * head
    h = shaft / 2
    pts_ = [(x1 + px * h, y1 + py * h), (bx + px * h, by + py * h), (bx + px * headw / 2, by + py * headw / 2), (x2, y2),
            (bx - px * headw / 2, by - py * headw / 2), (bx - px * h, by - py * h), (x1 - px * h, y1 - py * h)]
    poly(i, pts_, i.gradient(scale(color, 1.12), scale(color, 0.85)), OUT, 1.0)


def curved_arrow(i: Icon, cx, cy, r, start_deg, end_deg, color=RED, width=2.6, head=6.0):
    a0, a1 = math.radians(start_deg), math.radians(end_deg)
    steps = 24
    arc = [(cx + r * math.cos(a0 + (a1 - a0) * k / steps), cy + r * math.sin(a0 + (a1 - a0) * k / steps)) for k in range(steps + 1)]
    polyline(i, arc, OUT, width + 2.0)
    polyline(i, arc, color, width)
    ex, ey = arc[-1]
    tangent = a1 + (math.pi / 2 if a1 > a0 else -math.pi / 2)
    tx, ty = math.cos(tangent), math.sin(tangent)
    nx, ny = -ty, tx
    tip = (ex + tx * head * 0.75, ey + ty * head * 0.75)
    poly(i, [tip, (ex + nx * head * 0.55, ey + ny * head * 0.55), (ex - nx * head * 0.55, ey - ny * head * 0.55)], color, OUT, 1.0)


def badge(i: Icon, cx, cy, kind: str, color=GREEN, r=5.0):
    circle(i, cx, cy, r, i.gradient(scale(color, 1.15), scale(color, 0.85)), OUT, 1.0)
    line(i, cx - r * 0.55, cy, cx + r * 0.55, cy, WHITE, 1.8)
    if kind == "+":
        line(i, cx, cy - r * 0.55, cx, cy + r * 0.55, WHITE, 1.8)
    elif kind == "x":
        i.body.pop()
        line(i, cx - r * 0.45, cy - r * 0.45, cx + r * 0.45, cy + r * 0.45, WHITE, 1.8)
        line(i, cx - r * 0.45, cy + r * 0.45, cx + r * 0.45, cy - r * 0.45, WHITE, 1.8)


def dot(i: Icon, x, y, r=1.9, color=ORANGE):
    circle(i, x, y, r, color, OUT, 0.9)


def cursor(i: Icon, x, y, size=1.0):
    d = [(0, 0), (0, 14), (3.6, 10.8), (6.2, 16.2), (8.4, 15.2), (5.9, 9.9), (10.6, 9.6)]
    poly(i, [(x + px * size, y + py * size) for px, py in d], i.gradient(WHITE, "#c3c8ce"), OUT, 1.1)


# Standard object placements on the 32 px canvas.
def std_box(s: Scene, color=BOX, size=11.0, lift=0.0, **kw):
    h = size / 2
    box(s, -h, -h, lift, h, h, lift + size, color, **kw)


def cube_corners(size=11.0, lift=0.0):
    h = size / 2
    return [(x, y, z) for z in (lift, lift + size) for y in (-h, h) for x in (-h, h)]


# ---------------------------------------------------------------------------
# Icon registry
# ---------------------------------------------------------------------------
ICONS = {}


def icon(*names):
    def register(draw):
        for name in names:
            ICONS[name] = draw
        return draw
    return register


# ===========================================================================
# Tools
# ===========================================================================

@icon("tool-select")
def _(i):
    # A full-size selection cursor: its silhouette must read at 20 px.
    poly(i, [(6, 3), (26, 17), (17, 18.2), (21, 27), (16.6, 29), (12.5, 20), (6, 25)],
         i.gradient(WHITE, BOX_DARK), OUT, 1.2)


@icon("tool-camera")
def _(i):
    # Orbit: two arcs around a small scene, SketchUp's red and blue.
    s = Scene(16, 19)
    std_box(s, BOX, 8)
    s.render(i)
    curved_arrow(i, 16, 16, 12, 200, 330, RED, 2.2, 5.5)
    curved_arrow(i, 16, 16, 12, 20, 150, BLUE, 2.2, 5.5)


@icon("tool-translate")
def _(i):
    # Four planar arrows, unobscured by a large centre badge.
    for x, y in ((16, 2.5), (29.5, 16), (16, 29.5), (2.5, 16)):
        arrow(i, 16, 16, x, y, BOX, 2.6, 6.0)
    rect(i, 13.5, 13.5, 5, 5, WHITE, OUT, 1.0, 0.3)


@icon("tool-rotate")
def _(i):
    s = Scene(16, 21)
    std_box(s, BOX, 7)
    s.render(i)
    curved_arrow(i, 16, 16, 11.5, 145, 385, BOX, 2.8, 6.0)


@icon("tool-scale")
def _(i):
    # Nested bounds plus the diagonal growth arrow distinguish scale from move.
    rect(i, 5, 15, 12, 12, "none", BOX_DARK, 1.5, 0.4)
    polyline(i, [(5, 12), (5, 5), (27, 5), (27, 27), (20, 27)], BOX, 1.5)
    arrow(i, 11, 21, 25.5, 6.5, WHITE, 2.3, 5.5)


@icon("tool-pivot")
def _(i):
    circle(i, 16, 16, 11, "none", OUT, 3.4)
    circle(i, 16, 16, 11, "none", PURPLE, 2.0)
    for (x1, y1, x2, y2) in ((16, 2, 16, 30), (2, 16, 30, 16)):
        line(i, x1, y1, x2, y2, OUT, 3.2)
        line(i, x1, y1, x2, y2, scale(PURPLE, 1.35), 1.4)
    circle(i, 16, 16, 3.6, ORANGE, OUT, 1.0)


@icon("tool-entity")
def _(i):
    path(i, "M16 3 C10.2 3 7 7.4 7 11.6 C7 15.4 9.7 17.3 11.2 19.6 L11.7 21.5 L20.3 21.5 L20.8 19.6 C22.3 17.3 25 15.4 25 11.6 "
            "C25 7.4 21.8 3 16 3 Z", fill=i.radial("#fffbe0", YELLOW, 0.38, 0.32), width=1.1)
    rect(i, 11.7, 21.5, 8.6, 2.8, "#c9ced4", radius=0.8)
    rect(i, 12.4, 24.3, 7.2, 2.8, "#9aa1a9", radius=0.8)
    rect(i, 13.8, 27.1, 4.4, 1.8, "#6f767e", radius=0.8)
    polyline(i, [(12.6, 12.6), (14.3, 16.2), (16, 12.6), (17.7, 16.2), (19.4, 12.6)], "#b8770f", 1.1)


@icon("tool-block")
def _(i):
    # Keep all cube faces inside the canvas and omit the redundant plus badge.
    s = Scene(16, 22)
    std_box(s, BOX, 12)
    s.render(i)


@icon("tool-texture")
def _(i):
    s = Scene(16, 22)
    box(s, -6, -6, 0, 6, 6, 12, BOX)
    s.render(i)
    # Four broad checks remain distinct in a compact toolbar.
    for a, b in ((0, 0), (1, 1)):
        q = [s.p(-6 + a * 6, -6 + b * 6, 12), s.p(a * 6, -6 + b * 6, 12),
             s.p(a * 6, b * 6, 12), s.p(-6 + a * 6, b * 6, 12)]
        poly(i, q, "#697780", OUT, 0.6)


@icon("tool-apply-material", "material-apply")
def _(i):
    # Paint bucket pouring onto a face.
    s = Scene(19, 23)
    box(s, -6, -6, 0, 6, 6, 3, BOX)
    s.render(i)
    path(i, "M5 9 L14 4 L20 13 L11 18 Z", fill=i.gradient(YELLOW, scale(YELLOW, 0.75)), width=1.1)
    path(i, "M5 9 L11 18", stroke=OUT, width=1.0)
    path(i, "M14 4 C16 2 19 3 19.5 6", stroke=OUT, width=1.2)
    path(i, "M20 13 C22 14.5 23.5 17 23 20 C22.5 22 20.5 22 20.2 20 C20 18 21.2 16.2 20 13 Z", fill=RED, width=0.9)


@icon("tool-eyedropper", "material-pick")
def _(i):
    path(i, "M22.5 3.5 C24.5 1.5 28.5 5.5 26.5 7.5 L23.8 10.2 L21.8 8.2 Z", fill=i.gradient("#8b939c", "#4e555d"), width=1.1)
    path(i, "M21.8 8.2 L23.8 10.2 L11 23 L7.5 24.5 L9 21 Z", fill=i.gradient("#eaf4ff", SKY), width=1.1)
    path(i, "M9 21 L7.5 24.5 L6 26 C5 27 3.6 25.6 4.6 24.6 L6 23.1", fill=BLUE, width=0.9)
    rect(i, 17.5, 21, 11, 8, BLUE, OUT, 1.0, 1.2)


@icon("tool-decal")
def _(i):
    path(i, "M6 6 L26 6 L26 19 L19 26 L6 26 Z", fill=i.gradient("#ffd6ea", PINK), width=1.1)
    path(i, "M26 19 L20.5 19 C19.6 19 19 19.6 19 20.5 L19 26 Z", fill=scale(PINK, 0.75), width=1.0)
    circle(i, 13.5, 13.5, 3.8, WHITE, OUT, 0.9)
    path(i, "M9.5 21 L14 16.5 L17 19.5", stroke=WHITE, width=1.6)


@icon("tool-overlay")
def _(i):
    s = Scene(16, 20)
    box(s, -7, -7, 0, 7, 7, 1.5, BOX)
    s.render(i)
    q = [s.p(-5, -5, 1.6), s.p(5, -5, 1.6), s.p(5, 5, 1.6), s.p(-5, 5, 1.6)]
    poly(i, q, TEAL, OUT, 1.0, 0.9)
    arrow(i, 26, 4, 20, 12, RED, 2.2, 5.0, 5.0)


@icon("tool-clip")
def _(i):
    s = Scene(16, 22)
    std_box(s, BOX, 10)
    s.render(i)
    # The cut is the dominant symbol; no second miniature scissors icon.
    plane = [s.p(-7, 0.5, -2), s.p(7, 0.5, -2), s.p(7, 0.5, 13), s.p(-7, 0.5, 13)]
    poly(i, plane, BOX_DARK, "#9da7af", 1.1, 0.25)
    for a, b in ((s.p(-5, 0.5, 10), s.p(5, 0.5, 10)), (s.p(5, 0.5, 10), s.p(5, 0.5, 0))):
        line(i, a[0], a[1], b[0], b[1], OUT, 3.8)
        line(i, a[0], a[1], b[0], b[1], WHITE, 2.0)


@icon("tool-vertex")
def _(i):
    s = Scene(16, 18)
    std_box(s, BOX, 12)
    s.render(i)
    for c in cube_corners(12):
        if c[0] + c[1] + c[2] > -6:
            x, y = s.p(*c)
            rect(i, x - 2, y - 2, 4, 4, BLUE, OUT, 0.9, 0.5)


@icon("tool-path")
def _(i):
    d = "M4 26 C9 26 8 16 15 16 C22 16 20 6 28 6"
    path(i, d, stroke=OUT, width=4.2)
    path(i, d, stroke=TEAL, width=2.4)
    path(i, d, stroke=WHITE, width=0.8)
    for x, y in ((4, 26), (15, 16), (28, 6)):
        rect(i, x - 2.6, y - 2.6, 5.2, 5.2, i.gradient(WHITE, "#bfe9e3"), OUT, 1.0, 0.8)


@icon("tool-polygon")
def _(i):
    shape = [(16, 3.5), (27.5, 11.5), (23.5, 26.5), (8.5, 26.5), (4.5, 11.5)]
    poly(i, shape, i.gradient(mix(PURPLE, WHITE, 0.4), scale(PURPLE, 0.8)), OUT, 1.1)
    for x, y in shape:
        rect(i, x - 2, y - 2, 4, 4, WHITE, OUT, 0.9, 0.5)
    badge(i, 26, 26, "+")


@icon("tool-mirror")
def _(i):
    poly(i, [(4, 26), (14, 6), (14, 26)], i.gradient(mix(BLUE, WHITE, 0.35), scale(BLUE, 0.8)), OUT, 1.1)
    poly(i, [(28, 26), (18, 6), (18, 26)], "none", BLUE, 1.2, 0.5)
    line(i, 16, 3, 16, 29, RED, 1.6, dash="2.5 2")
    arrow(i, 8, 29, 24, 29, RED, 1.8, 4.2, 4.4)


@icon("tool-paint")
def _(i):
    s = Scene(18, 24)
    box(s, -7, -7, 0, 7, 7, 2, BOX)
    s.render(i)
    q = [s.p(-7, -7, 2.05), s.p(0, -7, 2.05), s.p(0, 7, 2.05), s.p(-7, 7, 2.05)]
    poly(i, q, GREEN, OUT, 0.8, 0.8)
    path(i, "M20 2.5 L27.5 10 L17 20.5 L9.5 13 Z", fill=i.gradient("#c9ced4", "#7b838c"), width=1.0)
    path(i, "M9.5 13 L17 20.5 L14 23.5 C12 25.5 7.5 25.5 5.5 24 C7.5 22.5 6 19 7 16.5 Z", fill=i.gradient(mix(GREEN, WHITE, 0.3), scale(GREEN, 0.8)), width=1.0)


@icon("tool-measure")
def _(i):
    poly(i, [(3, 22), (22, 3), (29, 10), (10, 29)], i.gradient(mix(YELLOW, WHITE, 0.3), scale(YELLOW, 0.8)), OUT, 1.1)
    for k in range(1, 8):
        x, y = 3 + k * 2.4, 22 - k * 2.4
        L = 3.2 if k % 2 == 0 else 1.8
        line(i, x, y, x + L * 0.707, y + L * 0.707, "#5f4304", 1.0)


@icon("tool-terrain")
def _(i):
    s = Scene(16, 18, 1.0)
    n = 6
    size = 20
    def h(u, v):
        return 5.0 * math.exp(-((u - 0.35) ** 2 + (v - 0.4) ** 2) * 6) + 3.0 * math.exp(-((u - 0.8) ** 2 + (v - 0.75) ** 2) * 9)
    for a in range(n):
        for b in range(n):
            u0, u1, v0, v1 = a / n, (a + 1) / n, b / n, (b + 1) / n
            q = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
            face = [((u - 0.5) * size, (v - 0.5) * size, h(u, v)) for u, v in q]
            s.face(face, GREEN, width=0.4)
    s.render(i)
    path(i, "M22 3 L26 9 L18 9 Z", fill=WHITE, width=0.8)


@icon("tool-patch")
def _(i):
    s = Scene(16, 20)
    n = 6
    for a in range(n):
        for b in range(n):
            u0, u1, v0, v1 = a / n, (a + 1) / n, b / n, (b + 1) / n
            q = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
            face = [((u - 0.5) * 18, (v - 0.5) * 18, 8 * math.sin(math.pi * u)) for u, v in q]
            s.face(face, PURPLE, width=0.4)
    s.render(i)


@icon("tool-extrude", "mesh-extrude")
def _(i):
    # SketchUp's push/pull: a lifted face with a red arrow.
    s = Scene(15, 21)
    box(s, -7, -7, 0, 7, 7, 5, BOX)
    box(s, -4, -4, 5, 4, 4, 10, ORANGE)
    s.render(i)
    arrow(i, 15, 12, 15, 1.5, RED, 2.8, 6.0)


@icon("tool-knife", "mesh-knife")
def _(i):
    s = Scene(14, 20)
    std_box(s, BOX, 11)
    s.render(i)
    polyline(i, [s.p(-5.5, 1, 11), s.p(0, 5.5, 8), s.p(5.5, 5.5, 3)], RED, 1.8, dash="2 1.6")
    path(i, "M18 17 L28.5 4.5 C30 5.5 30 7.5 29 9 L21 19.5 Z", fill=i.gradient("#f1f3f5", "#9aa1a9"), width=1.0)
    path(i, "M18 17 L21 19.5 L17.5 24 C16.5 25.3 14 23.5 15 22 Z", fill=BROWN, width=1.0)


@icon("tool-loop-cut", "mesh-loop-cut")
def _(i):
    s = Scene(16, 19)
    std_box(s, BOX, 12)
    s.render(i)
    ring = [s.p(0, -6, 12), s.p(0, 6, 12), s.p(0, 6, 0)]
    polyline(i, ring, OUT, 3.0)
    polyline(i, ring, ORANGE, 1.6)
    ring2 = [s.p(-6, 0, 12), s.p(6, 0, 12), s.p(6, 0, 0)]
    polyline(i, ring2, BLUE, 1.2, dash="2 1.5")


@icon("tool-workplane", "workplane")
def _(i):
    s = Scene(16, 21)
    box(s, -8, -8, 0, 8, 8, 0.6, BOX_DARK)
    s.render(i)
    for k in (-4, 0, 4):
        for a, b in ((s.p(k, -8, 0.7), s.p(k, 8, 0.7)), (s.p(-8, k, 0.7), s.p(8, k, 0.7))):
            line(i, a[0], a[1], b[0], b[1], "#647079", 0.8)
    # The explicit normal tells a workplane apart from a material tile.
    arrow(i, 16, 21, 16, 3, WHITE, 1.8, 5.0)


@icon("tool-curve", "curve-sweep")
def _(i):
    d = "M4 24 C8 8 24 26 28 8"
    path(i, d, stroke=OUT, width=6.0)
    path(i, d, stroke=TEAL, width=4.2)
    path(i, d, stroke=mix(TEAL, WHITE, 0.55), width=1.2)
    for x, y in ((4, 24), (28, 8)):
        circle(i, x, y, 2.6, WHITE, OUT, 1.0)


# ===========================================================================
# Primitives (the block tool's shapes)
# ===========================================================================

@icon("shape-box")
def _(i):
    s = Scene(16, 18)
    std_box(s, BOX, 12)
    s.render(i)


@icon("shape-wedge")
def _(i):
    s = Scene(16, 19)
    wedge(s, -6, -6, 0, 6, 6, 12, BOX)
    s.render(i)


@icon("shape-cylinder")
def _(i):
    s = Scene(16, 19)
    cylinder(s, 0, 0, 0, 12, 7, BOX)
    s.render(i)
    silhouette_cylinder(i, s, 0, 0, 0, 12, 7)


@icon("shape-cone", "shape-spike")
def _(i):
    s = Scene(16, 22)
    cone(s, 0, 0, 0, 16, 7.5, BOX)
    s.render(i)


@icon("shape-sphere")
def _(i):
    circle(i, 16, 16, 11.5, i.radial(WHITE, BOX_DARK, 0.36, 0.32), OUT, 1.1)
    path(i, "M4.8 16 C8 19.5 24 19.5 27.2 16", stroke=scale(BOX_DARK, 0.8), width=0.9)
    path(i, "M16 4.5 C12 8 12 24 16 27.5", stroke=scale(BOX_DARK, 0.8), width=0.9)


@icon("shape-arch")
def _(i):
    s = Scene(16, 20)
    arch(s, -4, 4, -8, 8, 0, 11, 4.5, BOX)
    s.render(i)


@icon("shape-stairs")
def _(i):
    s = Scene(16, 20)
    stairs(s, -6, 6, -8, 4, 3.2, 4, BOX)
    s.render(i)


@icon("shape-plane")
def _(i):
    s = Scene(16, 18)
    box(s, -9, -9, 0, 9, 9, 1, BOX)
    s.render(i)


@icon("shape-quad")
def _(i):
    s = Scene(16, 17)
    s.face([(-8, -8, 0), (8, -8, 0), (8, 8, 0), (-8, 8, 0)], BOX)
    s.render(i)


@icon("shape-torus")
def _(i):
    circle(i, 16, 17, 11.5, i.radial(WHITE, BOX_DARK, 0.4, 0.3), OUT, 1.1)
    i.add('<ellipse cx="16" cy="17" rx="4.8" ry="3" fill="#1b1d20" stroke="#1b1d20" stroke-width="1"/>')


# ===========================================================================
# Mesh and brush operations
# ===========================================================================

@icon("mesh-bevel")
def _(i):
    s = Scene(16, 19)
    b = 3.0
    h = 6.0
    top = 12.0
    # A box whose top front edges are chamfered.
    s.face([(-h, -h, 0), (-h, -h, top), (-h, h - b, top), (-h, h, top - b), (-h, h, 0)], BOX)
    s.face([(-h, h, 0), (-h, h, top - b), (h - b, h, top - b), (h, h - b, top - b)], BOX)
    s.face([(-h, -h, top), (h - b, -h, top), (h, -h + b, top - b), (h, h - b, top - b), (h - b, h, top - b), (-h, h, top - b), (-h, h - b, top)], BOX)
    s.face([(h, -h, 0), (h, h, 0), (h, h, top - b), (h, -h, top - b)], BOX)
    s.faces.clear()
    box(s, -h, -h, 0, h, h, top - b, BOX)
    s.face([(-h, -h, top - b), (h - b, -h, top - b), (h - b, h - b, top), (-h, h - b, top)], BOX)
    s.face([(h - b, -h, top - b), (h, -h, top - b), (h, h - b, top - b), (h - b, h - b, top)], ORANGE)
    s.face([(-h, h - b, top), (h - b, h - b, top), (h, h - b, top - b)], ORANGE)
    s.face([(-h, h - b, top), (h, h - b, top - b), (h, h, top - b), (-h, h, top - b)], ORANGE)
    s.render(i)


@icon("mesh-inset")
def _(i):
    s = Scene(16, 18)
    std_box(s, BOX, 12)
    s.render(i)
    inner = [s.p(-3, -3, 12), s.p(3, -3, 12), s.p(3, 3, 12), s.p(-3, 3, 12)]
    poly(i, inner, ORANGE, OUT, 1.0)
    for a, b in zip(inner, [s.p(-6, -6, 12), s.p(6, -6, 12), s.p(6, 6, 12), s.p(-6, 6, 12)]):
        line(i, a[0], a[1], b[0], b[1], OUT, 0.8)


@icon("mesh-bridge")
def _(i):
    s = Scene(16, 21)
    box(s, -9, -4, 0, -4, 4, 9, BOX)
    box(s, 4, -4, 0, 9, 4, 9, BOX)
    box(s, -4, -2, 5, 4, 2, 9, ORANGE)
    s.render(i)


@icon("mesh-slice")
def _(i):
    s = Scene(16, 19)
    box(s, -6, -6, 0, 6, 6, 5.4, BOX)
    box(s, -6, -6, 6.6, 6, 6, 12, BOX)
    s.render(i)
    a, b = s.p(-9, -9, 6), s.p(9, -9, 6)
    line(i, 2, 16.5, 30, 16.5, RED, 1.6, dash="3 1.8")


@icon("mesh-subdivide")
def _(i):
    s = Scene(16, 18)
    std_box(s, BOX, 12)
    s.render(i)
    for k in (-2, 2):
        for face in ((lambda t: [(t, -6, 12), (t, 6, 12)]), (lambda t: [(-6, t, 12), (6, t, 12)]),
                     (lambda t: [(6, t, 0), (6, t, 12)]), (lambda t: [(t, 6, 0), (t, 6, 12)]),
                     (lambda t: [(6, -6, 6 + t * 1.5), (6, 6, 6 + t * 1.5)]), (lambda t: [(-6, 6, 6 + t * 1.5), (6, 6, 6 + t * 1.5)])):
            a, b = face(k)
            pa, pb = s.p(*a), s.p(*b)
            line(i, pa[0], pa[1], pb[0], pb[1], scale(BOX_DARK, 0.7), 0.8)


@icon("mesh-smooth")
def _(i):
    circle(i, 16, 17, 11, i.radial(WHITE, BOX_DARK, 0.4, 0.3), OUT, 1.1)
    path(i, "M5 17 C9 21 23 21 27 17", stroke=scale(BOX_DARK, 0.75), width=0.8)
    path(i, "M16 6 C12 9 12 25 16 28", stroke=scale(BOX_DARK, 0.75), width=0.8)
    path(i, "M24 2.5 L25.2 5.8 L28.5 7 L25.2 8.2 L24 11.5 L22.8 8.2 L19.5 7 L22.8 5.8 Z", fill=YELLOW, width=0.8)


@icon("mesh-solidify")
def _(i):
    s = Scene(16, 20)
    box(s, -8, -8, 0, 8, 8, 3, BOX)
    s.render(i)
    q = [s.p(-8, -8, 7), s.p(8, -8, 7), s.p(8, 8, 7), s.p(-8, 8, 7)]
    poly(i, q, "none", BLUE, 1.2, 1.0)
    arrow(i, 16, 13, 16, 4, RED, 2.2, 5.0)


@icon("mesh-merge", "vertex-weld")
def _(i):
    for x, y in ((6, 7), (26, 7), (6, 25), (26, 25)):
        arrow(i, x, y, 16 + (x - 16) * 0.35, 16 + (y - 16) * 0.35, BLUE, 1.8, 4.0, 4.2)
    circle(i, 16, 16, 3.6, ORANGE, OUT, 1.0)


@icon("mesh-collapse")
def _(i):
    poly(i, [(4, 8), (28, 8), (22, 24), (10, 24)], i.gradient(mix(BLUE, WHITE, 0.4), scale(BLUE, 0.85)), OUT, 1.0)
    arrow(i, 5, 16, 13, 16, RED, 1.8, 4.2, 4.4)
    arrow(i, 27, 16, 19, 16, RED, 1.8, 4.2, 4.4)
    circle(i, 16, 16, 2.4, ORANGE, OUT, 0.9)


@icon("mesh-dissolve")
def _(i):
    poly(i, [(4, 6), (28, 6), (28, 26), (4, 26)], i.gradient(mix(BLUE, WHITE, 0.4), scale(BLUE, 0.85)), OUT, 1.0)
    line(i, 16, 7, 16, 25, RED, 1.6, dash="2.2 1.8")
    badge(i, 25, 25, "-", RED, 4.4)


@icon("mesh-split")
def _(i):
    poly(i, [(3, 7), (14.5, 7), (14.5, 25), (3, 25)], i.gradient(mix(BLUE, WHITE, 0.4), scale(BLUE, 0.85)), OUT, 1.0)
    poly(i, [(17.5, 7), (29, 7), (29, 25), (17.5, 25)], i.gradient(mix(BLUE, WHITE, 0.4), scale(BLUE, 0.85)), OUT, 1.0)
    arrow(i, 12, 16, 5, 16, RED, 1.6, 3.8, 4.0)
    arrow(i, 20, 16, 27, 16, RED, 1.6, 3.8, 4.0)


@icon("mesh-triangulate")
def _(i):
    poly(i, [(4, 6), (28, 6), (28, 26), (4, 26)], i.gradient(mix(BLUE, WHITE, 0.4), scale(BLUE, 0.85)), OUT, 1.0)
    line(i, 4, 26, 28, 6, OUT, 1.2)
    line(i, 16, 6, 4, 26, OUT, 0.8)
    line(i, 28, 26, 16, 6, OUT, 0.8)


@icon("mesh-flip-normals")
def _(i):
    poly(i, [(4, 20), (18, 13), (28, 18), (14, 25)], i.gradient(mix(BLUE, WHITE, 0.3), scale(BLUE, 0.8)), OUT, 1.0)
    arrow(i, 16, 19, 16, 4, GREEN, 2.0, 5.0)
    curved_arrow(i, 23, 9, 5, 200, 20, RED, 1.6, 4.0)


@icon("mesh-fill-hole")
def _(i):
    s = Scene(16, 18)
    std_box(s, BOX, 12)
    s.render(i)
    hole = [s.p(-3, -3, 12), s.p(3, -3, 12), s.p(3, 3, 12), s.p(-3, 3, 12)]
    poly(i, hole, GREEN, OUT, 1.0, 0.9)
    badge(i, 25, 25, "+")


@icon("boolean-union")
def _(i):
    s = Scene(16, 20)
    box(s, -8, -4, 0, 2, 6, 9, BLUE)
    box(s, -2, -6, 0, 8, 4, 12, BLUE)
    s.render(i)
    badge(i, 26, 26, "+")


@icon("boolean-subtract")
def _(i):
    s = Scene(15, 20)
    box(s, -7, -7, 0, 7, 7, 10, BOX)
    s.render(i)
    circle(i, 21, 10, 6.5, i.radial(mix(RED, WHITE, 0.4), scale(RED, 0.85)), OUT, 1.1)
    badge(i, 26, 26, "-", RED)


@icon("boolean-intersect")
def _(i):
    circle(i, 12, 16, 9, "none", BLUE, 1.6)
    circle(i, 20, 16, 9, "none", BLUE, 1.6)
    path(i, "M16 8.4 A9 9 0 0 1 16 23.6 A9 9 0 0 1 16 8.4 Z", fill=ORANGE, width=1.0)


@icon("mesh-to-brush")
def _(i):
    circle(i, 9, 12, 6.5, i.radial(WHITE, BOX_DARK), OUT, 1.0)
    arrow(i, 14, 18, 20, 18, RED, 1.8, 4.2, 4.4)
    s = Scene(24, 24)
    box(s, -4, -4, 0, 4, 4, 8, ORANGE)
    s.render(i)


@icon("brush-to-mesh")
def _(i):
    s = Scene(9, 17)
    box(s, -4, -4, 0, 4, 4, 8, ORANGE)
    s.render(i)
    arrow(i, 13, 18, 19, 18, RED, 1.8, 4.2, 4.4)
    circle(i, 24, 19, 6.5, i.radial(WHITE, BLUE), OUT, 1.0)


@icon("csg-hollow")
def _(i):
    s = Scene(16, 18)
    std_box(s, ORANGE, 12)
    s.render(i)
    inner = [s.p(-3.5, -3.5, 12), s.p(3.5, -3.5, 12), s.p(3.5, 3.5, 12), s.p(-3.5, 3.5, 12)]
    poly(i, inner, scale(ORANGE, 0.35), OUT, 1.0)


@icon("csg-carve")
def _(i):
    s = Scene(14, 20)
    box(s, -7, -7, 0, 7, 7, 9, ORANGE)
    s.render(i)
    s2 = Scene(21, 12)
    box(s2, -4, -4, 0, 4, 4, 8, RED, opacity=0.8)
    s2.render(i)


@icon("csg-merge")
def _(i):
    s = Scene(16, 20)
    box(s, -9, -4, 0, -1, 4, 9, ORANGE)
    box(s, 1, -4, 0, 9, 4, 9, ORANGE)
    s.render(i)
    arrow(i, 4, 28, 12, 28, RED, 1.6, 3.8, 4.0)
    arrow(i, 28, 28, 20, 28, RED, 1.6, 3.8, 4.0)


@icon("snap-to-grid")
def _(i):
    rect(i, 3, 18, 26, 11, "#2c3137", "#8c949c", 1.0, 1.2)
    for k in (9.5, 16, 22.5):
        line(i, k, 18.5, k, 28.5, "#5f666e", 1.0)
    s = Scene(16, 13)
    box(s, -4, -4, 0, 4, 4, 7, ORANGE)
    s.render(i)
    arrow(i, 27, 3, 27, 13, RED, 1.6, 4.0, 4.2)


def _flip(i, vertical: bool):
    if vertical:
        group(i, "rotate(90 16 16)")
    poly(i, [(3, 26), (14, 6), (14, 26)], i.gradient(mix(PURPLE, WHITE, 0.4), scale(PURPLE, 0.8)), OUT, 1.1)
    poly(i, [(29, 26), (18, 6), (18, 26)], "none", PURPLE, 1.3)
    line(i, 16, 3, 16, 29, RED, 1.5, dash="2.5 2")
    if vertical:
        end(i)


@icon("transform-flip-horizontal")
def _(i):
    _flip(i, False)


@icon("transform-flip-vertical")
def _(i):
    _flip(i, True)


def _rotate90(i, clockwise: bool):
    s = Scene(16, 21)
    box(s, -5, -5, 0, 5, 5, 9, BOX)
    s.render(i)
    if clockwise:
        curved_arrow(i, 16, 14, 11, 200, 330, RED, 2.2, 5.4)
    else:
        curved_arrow(i, 16, 14, 11, 340, 210, RED, 2.2, 5.4)
    text(i, 16, 30.5, "90", 7, OUT, 800)


@icon("transform-rotate-cw")
def _(i):
    _rotate90(i, True)


@icon("transform-rotate-ccw")
def _(i):
    _rotate90(i, False)


def _align(i, side: str):
    line_x = {"left": 5, "right": 27}.get(side)
    line_y = {"top": 5, "bottom": 27}.get(side)
    if line_x is not None:
        line(i, line_x, 3, line_x, 29, RED, 2.0)
        w = (12, 18, 8)
        for k, width in enumerate(w):
            x = line_x + 2 if side == "left" else line_x - 2 - width
            rect(i, x, 6 + k * 7.5, width, 5.5, i.gradient(WHITE, BOX_DARK), OUT, 1.0, 1.0)
    else:
        line(i, 3, line_y, 29, line_y, RED, 2.0)
        h = (12, 18, 8)
        for k, height in enumerate(h):
            y = line_y + 2 if side == "top" else line_y - 2 - height
            rect(i, 6 + k * 7.5, y, 5.5, height, i.gradient(WHITE, BOX_DARK), OUT, 1.0, 1.0)


for _side in ("left", "right", "top", "bottom"):
    ICONS[f"align-{_side}"] = (lambda side: (lambda i: _align(i, side)))(_side)


# ===========================================================================
# Selection modes and selection operations
# ===========================================================================

def _wire_cube(i, s: Scene, size=12, color="#8f97a0", width=1.1):
    corners = cube_corners(size)
    idx = lambda x, y, z: corners.index((x, y, z))
    h = size / 2
    edges = []
    for x in (-h, h):
        for y in (-h, h):
            edges.append(((x, y, 0), (x, y, size)))
    for z in (0, size):
        for x in (-h, h):
            edges.append(((x, -h, z), (x, h, z)))
        for y in (-h, h):
            edges.append(((-h, y, z), (h, y, z)))
    for a, b in edges:
        hidden = (a[0] == -h and b[0] == -h and a[1] == -h and b[1] == -h) or (a[2] == 0 and b[2] == 0 and (a[0] == -h and b[0] == -h or a[1] == -h and b[1] == -h))
        pa, pb = s.p(*a), s.p(*b)
        line(i, pa[0], pa[1], pb[0], pb[1], color, width * (0.7 if hidden else 1.0), dash="1.5 1.5" if hidden else "")
    return corners


@icon("select-vertices")
def _(i):
    s = Scene(16, 18)
    corners = _wire_cube(i, s)
    for c in corners:
        if not (c[0] < 0 and c[1] < 0 and c[2] == 0):
            x, y = s.p(*c)
            dot(i, x, y, 2.3, ORANGE)


@icon("select-edges")
def _(i):
    s = Scene(16, 18)
    _wire_cube(i, s)
    for a, b in (((6, -6, 12), (6, 6, 12)), ((6, 6, 12), (6, 6, 0)), ((6, 6, 12), (-6, 6, 12))):
        pa, pb = s.p(*a), s.p(*b)
        line(i, pa[0], pa[1], pb[0], pb[1], OUT, 3.8)
        line(i, pa[0], pa[1], pb[0], pb[1], ORANGE, 2.4)


@icon("select-faces")
def _(i):
    s = Scene(16, 18)
    box(s, -6, -6, 0, 6, 6, 12, "#5b636c")
    s.render(i)
    top = [s.p(-6, -6, 12), s.p(6, -6, 12), s.p(6, 6, 12), s.p(-6, 6, 12)]
    poly(i, top, i.gradient(mix(ORANGE, WHITE, 0.35), ORANGE), OUT, 1.0)


@icon("select-meshes")
def _(i):
    s = Scene(16, 18)
    std_box(s, BLUE, 12)
    s.render(i)


@icon("select-objects")
def _(i):
    s = Scene(12, 18)
    box(s, -5, -5, 0, 5, 5, 10, BLUE)
    s.render(i)
    circle(i, 23, 22, 6, i.radial(mix(GREEN, WHITE, 0.5), scale(GREEN, 0.8)), OUT, 1.0)


@icon("select-groups")
def _(i):
    for cx, cy, color in ((10, 14, ORANGE), (22, 14, BLUE), (16, 25, GREEN)):
        s = Scene(cx, cy)
        box(s, -3, -3, 0, 3, 3, 6, color)
        s.render(i)
    polyline(i, [(4, 3), (2, 3), (2, 29), (4, 29)], "#c3c8ce", 1.4)
    polyline(i, [(28, 3), (30, 3), (30, 29), (28, 29)], "#c3c8ce", 1.4)


@icon("select-navigation")
def _(i):
    tri = [(3, 26), (16, 4), (29, 26)]
    poly(i, tri, i.gradient(mix(GREEN, WHITE, 0.35), scale(GREEN, 0.8)), OUT, 1.1, 0.95)
    for a, b in (((9.5, 15), (22.5, 15)), ((9.5, 15), (16, 26)), ((22.5, 15), (16, 26))):
        line(i, *a, *b, "#1d4d18", 1.0)
    path(i, "M12 29 C14 24 18 24 20 20", stroke=OUT, width=3.2)
    path(i, "M12 29 C14 24 18 24 20 20", stroke=WHITE, width=1.6)


def _selection_square(i, fill=None):
    rect(i, 5, 5, 22, 22, fill if fill is not None else "none", BLUE, 1.6, 1.5)


@icon("select-all")
def _(i):
    rect(i, 3, 3, 26, 26, "none", BLUE, 1.5, 1.5)
    for x, y in ((6, 6), (17, 6), (6, 17), (17, 17)):
        rect(i, x, y, 9, 9, i.gradient(mix(BLUE, WHITE, 0.5), BLUE), OUT, 1.0, 1.0)


@icon("select-none")
def _(i):
    rect(i, 3, 3, 26, 26, "none", "#8f97a0", 1.4, 1.5)
    for x, y in ((6, 6), (17, 6), (6, 17), (17, 17)):
        rect(i, x, y, 9, 9, i.gradient(WHITE, BOX_DARK), OUT, 1.0, 1.0)
    badge(i, 24, 24, "x", RED, 5)


@icon("select-invert")
def _(i):
    for k, (x, y) in enumerate(((5, 5), (17, 5), (5, 17), (17, 17))):
        fill = i.gradient(mix(BLUE, WHITE, 0.5), BLUE) if k in (0, 3) else i.gradient(WHITE, BOX_DARK)
        rect(i, x, y, 10, 10, fill, OUT, 1.0, 1.0)
    curved_arrow(i, 16, 16, 13, 300, 420, RED, 1.6, 4.0)


@icon("select-grow")
def _(i):
    rect(i, 11, 11, 10, 10, i.gradient(mix(ORANGE, WHITE, 0.3), ORANGE), OUT, 1.0, 1.0)
    rect(i, 5, 5, 22, 22, "none", ORANGE, 1.2, 1.0)
    for x, y, tx, ty in ((11, 11, 4, 4), (21, 11, 28, 4), (11, 21, 4, 28), (21, 21, 28, 28)):
        arrow(i, x, y, tx, ty, RED, 1.4, 3.6, 3.8)


@icon("select-shrink")
def _(i):
    rect(i, 12.5, 12.5, 7, 7, i.gradient(mix(ORANGE, WHITE, 0.3), ORANGE), OUT, 1.0, 1.0)
    rect(i, 4, 4, 24, 24, "none", ORANGE, 1.2, 1.0)
    for x, y, tx, ty in ((4, 4, 11, 11), (28, 4, 21, 11), (4, 28, 11, 21), (28, 28, 21, 21)):
        arrow(i, x, y, tx, ty, RED, 1.4, 3.6, 3.8)


@icon("select-loop")
def _(i):
    circle(i, 16, 16, 11, i.radial(WHITE, BOX_DARK), OUT, 1.0)
    i.add('<ellipse cx="16" cy="16" rx="11" ry="3.6" fill="none" stroke="#1b1d20" stroke-width="3.6"/>')
    i.add(f'<ellipse cx="16" cy="16" rx="11" ry="3.6" fill="none" stroke="{ORANGE}" stroke-width="2.2"/>')


@icon("select-ring")
def _(i):
    circle(i, 16, 16, 11, i.radial(WHITE, BOX_DARK), OUT, 1.0)
    for k in range(-4, 5, 2):
        x = 16 + k * 2.4
        line(i, x, 16 - 3.2 * math.sqrt(max(0, 1 - (k / 4.6) ** 2)) - 0.5, x, 16 + 3.2 * math.sqrt(max(0, 1 - (k / 4.6) ** 2)) + 0.5, ORANGE, 2.0)


@icon("select-touching")
def _(i):
    s = Scene(16, 21)
    box(s, -9, -4, 0, 0, 4, 8, ORANGE)
    box(s, 0, -4, 0, 9, 4, 8, BOX)
    s.render(i)
    cursor(i, 18, 3, 0.8)


@icon("select-same-material")
def _(i):
    for x, y in ((4, 4), (18, 4), (11, 18)):
        rect(i, x, y, 10, 10, i.gradient(BLUE, scale(BLUE, 0.75)), ORANGE, 1.6, 1.0)
        rect(i, x + 2, y + 2, 3, 3, mix(BLUE, WHITE, 0.6), "none", 0, 0)


@icon("select-same-class")
def _(i):
    for x, y in ((9, 10), (23, 10), (16, 23)):
        circle(i, x, y, 5.2, i.radial("#fffbe0", YELLOW), ORANGE, 1.6)


# ===========================================================================
# UVs and materials
# ===========================================================================

def _uv_face(i, fill_checker=True):
    rect(i, 5, 5, 22, 22, "#2c3137", "#8c949c", 1.0, 1.0)
    for a in range(4):
        for b in range(4):
            if (a + b) % 2 == 0:
                rect(i, 5 + a * 5.5, 5 + b * 5.5, 5.5, 5.5, BLUE, "none", 0, 0)
    rect(i, 5, 5, 22, 22, "none", OUT, 1.0, 1.0)


@icon("uv-fit")
def _(i):
    _uv_face(i)
    for d in ("M2 9 L2 2 L9 2", "M23 2 L30 2 L30 9", "M30 23 L30 30 L23 30", "M9 30 L2 30 L2 23"):
        path(i, d, stroke=OUT, width=3.4)
        path(i, d, stroke=RED, width=1.8)


@icon("uv-align-world")
def _(i):
    _uv_face(i)
    circle(i, 23, 23, 6.5, i.radial(mix(TEAL, WHITE, 0.5), scale(TEAL, 0.8)), OUT, 1.0)
    path(i, "M16.5 23 L29.5 23 M23 16.5 C20 20 20 26 23 29.5 M23 16.5 C26 20 26 26 23 29.5", stroke=OUT, width=0.8)


@icon("uv-align-face")
def _(i):
    s = Scene(16, 19)
    std_box(s, BOX, 12)
    s.render(i)
    q = [s.p(6.05, -6, 0), s.p(6.05, 6, 0), s.p(6.05, 6, 12), s.p(6.05, -6, 12)]
    poly(i, q, BLUE, OUT, 1.0, 0.85)
    arrow(i, 27, 27, 22, 22, RED, 1.6, 4.0, 4.0)


def _justify(i, where):
    rect(i, 3, 3, 26, 26, "#2c3137", "#8c949c", 1.0, 1.0)
    positions = {"left": (4, 11), "right": (18, 11), "top": (11, 4), "bottom": (11, 18), "center": (11, 11)}
    x, y = positions[where]
    rect(i, x, y, 10, 10, i.gradient(mix(BLUE, WHITE, 0.4), BLUE), OUT, 1.0, 0.8)
    if where in ("left", "right"):
        lx = 4 if where == "left" else 28
        line(i, lx, 4, lx, 28, RED, 1.8)
    elif where in ("top", "bottom"):
        ly = 4 if where == "top" else 28
        line(i, 4, ly, 28, ly, RED, 1.8)
    else:
        line(i, 16, 4, 16, 28, RED, 1.2, dash="2 1.5")
        line(i, 4, 16, 28, 16, RED, 1.2, dash="2 1.5")


for _where in ("left", "right", "top", "bottom", "center"):
    ICONS[f"uv-justify-{_where}"] = (lambda where: (lambda i: _justify(i, where)))(_where)


@icon("uv-rotate")
def _(i):
    _uv_face(i)
    curved_arrow(i, 16, 16, 12.5, 200, 340, RED, 2.2, 5.0)


@icon("uv-scale")
def _(i):
    _uv_face(i)
    arrow(i, 11, 21, 27.5, 4.5, RED, 2.2, 5.4)


@icon("uv-shift")
def _(i):
    _uv_face(i)
    arrow(i, 8, 16, 29, 16, RED, 2.2, 5.2)


@icon("uv-islands")
def _(i):
    rect(i, 3, 3, 26, 26, "#2c3137", "#8c949c", 1.0, 1.0)
    poly(i, [(6, 6), (15, 6), (15, 12), (6, 15)], BLUE, OUT, 1.0)
    poly(i, [(18, 6), (26, 6), (26, 16), (20, 14)], GREEN, OUT, 1.0)
    poly(i, [(7, 18), (16, 17), (14, 26), (6, 26)], ORANGE, OUT, 1.0)
    poly(i, [(19, 19), (26, 19), (26, 26), (19, 26)], PURPLE, OUT, 1.0)


@icon("material-replace")
def _(i):
    rect(i, 3, 4, 12, 12, i.gradient(BLUE, scale(BLUE, 0.75)), OUT, 1.0, 1.0)
    rect(i, 17, 16, 12, 12, i.gradient(ORANGE, scale(ORANGE, 0.75)), OUT, 1.0, 1.0)
    curved_arrow(i, 16, 16, 9, 250, 350, RED, 1.8, 4.2)


@icon("texture-lock")
def _(i):
    rect(i, 6, 14, 20, 15, i.gradient(mix(YELLOW, WHITE, 0.3), scale(YELLOW, 0.8)), OUT, 1.1, 2.0)
    d = "M10.5 14 L10.5 10 C10.5 6 13 3.5 16 3.5 C19 3.5 21.5 6 21.5 10 L21.5 14"
    path(i, d, stroke=OUT, width=3.8)
    path(i, d, stroke="#c9ced4", width=2.2)
    for x, y in ((9, 17), (17, 21.5)):
        rect(i, x, y, 6, 5, BLUE, OUT, 0.7, 0.4)


@icon("texture-scale-lock")
def _(i):
    ICONS["texture-lock"](i)
    arrow(i, 20, 27, 29.5, 17.5, RED, 1.6, 3.8, 4.0)


# ===========================================================================
# Terrain and surfaces
# ===========================================================================

def _terrain_base(i, bump):
    s = Scene(16, 21)
    n = 5
    for a in range(n):
        for b in range(n):
            u0, u1, v0, v1 = a / n, (a + 1) / n, b / n, (b + 1) / n
            q = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
            s.face([((u - 0.5) * 20, (v - 0.5) * 20, bump(u, v)) for u, v in q], GREEN, width=0.4)
    s.render(i)


@icon("terrain-raise")
def _(i):
    _terrain_base(i, lambda u, v: 6 * math.exp(-((u - 0.5) ** 2 + (v - 0.5) ** 2) * 10))
    arrow(i, 26, 16, 26, 3, RED, 2.2, 5.0)


@icon("terrain-lower")
def _(i):
    _terrain_base(i, lambda u, v: -4 * math.exp(-((u - 0.5) ** 2 + (v - 0.5) ** 2) * 10))
    arrow(i, 26, 3, 26, 16, RED, 2.2, 5.0)


@icon("terrain-smooth")
def _(i):
    _terrain_base(i, lambda u, v: 2.5 * math.sin(u * math.pi))
    path(i, "M20 3 L21.4 6.6 L25 8 L21.4 9.4 L20 13 L18.6 9.4 L15 8 L18.6 6.6 Z", fill=YELLOW, width=0.8)


@icon("terrain-flatten")
def _(i):
    _terrain_base(i, lambda u, v: 0.0)
    line(i, 3, 6, 29, 6, RED, 2.0)
    arrow(i, 16, 7, 16, 13, RED, 1.6, 3.8, 4.0)


@icon("terrain-paint")
def _(i):
    _terrain_base(i, lambda u, v: 3 * math.exp(-((u - 0.4) ** 2 + (v - 0.5) ** 2) * 6))
    path(i, "M22 2 L29 9 L21 17 L14 10 Z", fill=i.gradient("#c9ced4", "#7b838c"), width=1.0)
    path(i, "M14 10 L21 17 L19 19 C17.5 20.5 14 20.5 12.5 19.5 C14 18.5 13 16 13.5 14 Z", fill=BROWN, width=1.0)


@icon("displacement")
def _(i):
    _terrain_base(i, lambda u, v: 3 * math.sin(u * 2 * math.pi) * math.sin(v * math.pi))
    for x, y in ((6, 6), (26, 6)):
        rect(i, x - 2, y - 2, 4, 4, BLUE, OUT, 0.9, 0.5)


# ===========================================================================
# Entities
# ===========================================================================

@icon("entity-point")
def _(i):
    s = Scene(16, 22)
    box(s, -5, -5, 0, 5, 5, 10, GREEN, opacity=0.9)
    s.render(i)
    o = s.p(0, 0, 5)
    for axis, e in ((0, (8, 0, 5)), (1, (0, 8, 5)), (2, (0, 0, 14))):
        p = s.p(*e)
        line(i, o[0], o[1], p[0], p[1], AXIS[axis], 1.6)


@icon("entity-brush")
def _(i):
    s = Scene(16, 20)
    std_box(s, TEAL, 11)
    s.render(i)
    ICONS["entity-point-mini"](i)


@icon("entity-point-mini")
def _(i):
    circle(i, 25, 25, 5, i.radial("#fffbe0", YELLOW), OUT, 1.0)


@icon("entity-light")
def _(i):
    ICONS["filter-lights"](i)


@icon("entity-spawn")
def _(i):
    circle(i, 16, 7, 4.2, i.radial(mix(GREEN, WHITE, 0.4), GREEN), OUT, 1.0)
    path(i, "M9 28 L10.5 15.5 C11 13 13 12 16 12 C19 12 21 13 21.5 15.5 L23 28 Z", fill=i.gradient(mix(GREEN, WHITE, 0.3), scale(GREEN, 0.8)), width=1.0)
    i.add(f'<ellipse cx="16" cy="28.5" rx="11" ry="2.4" fill="none" stroke="{GREEN}" stroke-width="1.4"/>')


@icon("entity-trigger")
def _(i):
    ICONS["filter-triggers"](i)


@icon("entity-sound")
def _(i):
    path(i, "M4 12 L10 12 L17 5.5 L17 26.5 L10 20 L4 20 Z", fill=i.gradient("#c9ced4", "#7b838c"), width=1.1)
    for r in (5, 9):
        path(i, f"M{20 + r * 0.2} {16 - r} A{r} {r} 0 0 1 {20 + r * 0.2} {16 + r}", stroke=BLUE, width=2.0)


@icon("entity-prop")
def _(i):
    ICONS["filter-props"](i)


@icon("entity-camera")
def _(i):
    rect(i, 3, 10, 19, 14, i.gradient("#8d949c", "#4f565e"), OUT, 1.1, 2.0)
    poly(i, [(22, 13.5), (29, 9.5), (29, 24.5), (22, 20.5)], "#4f565e")
    circle(i, 12.5, 17, 4.6, i.radial(SKY, scale(BLUE, 0.7)), OUT, 1.0)
    rect(i, 5, 7, 5, 3, "#4f565e", OUT, 1.0, 0.8)


@icon("entity-logic")
def _(i):
    rect(i, 4, 6, 24, 20, i.gradient("#434a52", "#2a2f35"), OUT, 1.1, 3.0)
    for y in (11, 16, 21):
        circle(i, 4, y, 1.8, GREEN, OUT, 0.8)
        circle(i, 28, y, 1.8, ORANGE, OUT, 0.8)
    text(i, 16, 19.5, "IO", 8.5, WHITE)


@icon("entity-path-node")
def _(i):
    path(i, "M5 27 L16 16 L27 5", stroke=TEAL, width=2.0)
    for x, y in ((5, 27), (16, 16), (27, 5)):
        i.add(f'<rect x="{x - 3}" y="{y - 3}" width="6" height="6" transform="rotate(45 {x} {y})" fill="{mix(TEAL, WHITE, 0.4)}" stroke="{OUT}" stroke-width="1"/>')


# ===========================================================================
# File, edit, history
# ===========================================================================

def _page(i, fold=True):
    path(i, "M7 3 L19 3 L25 9 L25 29 L7 29 Z", fill=i.gradient(WHITE, "#c9ced4"), width=1.1)
    if fold:
        path(i, "M19 3 L19 9 L25 9", fill="#aab1b9", width=1.0)


@icon("file-new")
def _(i):
    _page(i)
    badge(i, 23, 24, "+")


@icon("file-open")
def _(i):
    path(i, "M3 8 L3 26 L25 26 L25 11 L14 11 L11.5 8 Z", fill=scale(YELLOW, 0.8), width=1.1)
    path(i, "M3 26 L7.5 14 L29 14 L25 26 Z", fill=i.gradient(mix(YELLOW, WHITE, 0.35), YELLOW), width=1.1)


@icon("file-save")
def _(i):
    path(i, "M4 4 L23.5 4 L28 8.5 L28 28 L4 28 Z", fill=i.gradient(mix(BLUE, WHITE, 0.15), scale(BLUE, 0.75)), width=1.1)
    rect(i, 9, 4, 13, 8.5, "#dbe7f5", OUT, 1.0, 0.6)
    rect(i, 17.5, 5.5, 3, 5.5, scale(BLUE, 0.7), OUT, 0.6, 0.3)
    rect(i, 8, 17, 16, 11, WHITE, OUT, 1.0, 0.6)
    line(i, 10.5, 21, 21.5, 21, "#9aa1a9", 1.0)
    line(i, 10.5, 24.5, 18.5, 24.5, "#9aa1a9", 1.0)


@icon("file-save-as")
def _(i):
    ICONS["file-save"](i)
    path(i, "M18 30 L19 26 L27.5 17.5 L30.5 20.5 L22 29 Z", fill=YELLOW, width=1.0)


@icon("file-close")
def _(i):
    _page(i)
    badge(i, 23, 24, "x", RED)


@icon("file-import")
def _(i):
    _page(i)
    arrow(i, 2, 18, 16, 18, GREEN, 2.4, 5.4)


@icon("file-export")
def _(i):
    _page(i)
    arrow(i, 16, 18, 30, 18, BLUE, 2.4, 5.4)


def _history_arrow(i, flip: bool):
    d = "M26 23 C26 14 20.5 10 12 10 L12 5 L3.5 12.5 L12 20 L12 15 C17.5 15 22 17 26 23 Z"
    if flip:
        group(i, "translate(32,0) scale(-1,1)")
    path(i, d, fill=i.gradient(mix(BLUE, WHITE, 0.3), scale(BLUE, 0.8)), width=1.1)
    if flip:
        end(i)


@icon("edit-undo")
def _(i):
    _history_arrow(i, False)


@icon("edit-redo")
def _(i):
    _history_arrow(i, True)


@icon("edit-cut")
def _(i):
    line(i, 10, 3, 21, 20, OUT, 3.4)
    line(i, 22, 3, 11, 20, OUT, 3.4)
    line(i, 10, 3, 21, 20, "#e3e6e9", 1.8)
    line(i, 22, 3, 11, 20, "#e3e6e9", 1.8)
    circle(i, 9.5, 24, 4.4, "none", RED, 2.4)
    circle(i, 22.5, 24, 4.4, "none", RED, 2.4)


@icon("edit-copy")
def _(i):
    rect(i, 11, 3, 17, 20, "#aab1b9", OUT, 1.0, 1.5)
    rect(i, 4, 9, 17, 20, i.gradient(WHITE, "#c9ced4"), OUT, 1.0, 1.5)
    for y in (15, 19, 23):
        line(i, 8, y, 17, y, "#9aa1a9", 1.0)


@icon("edit-paste")
def _(i):
    rect(i, 5, 5, 22, 24, i.gradient(mix(BROWN, WHITE, 0.35), BROWN), OUT, 1.1, 2.0)
    rect(i, 11, 2.5, 10, 5, "#c9ced4", OUT, 1.0, 1.2)
    rect(i, 9, 11, 14, 15, WHITE, OUT, 1.0, 0.8)
    for y in (15, 19, 22.5):
        line(i, 11.5, y, 20.5, y, "#9aa1a9", 1.0)


@icon("edit-delete")
def _(i):
    rect(i, 7, 10, 18, 19, i.gradient(mix(RED, WHITE, 0.2), scale(RED, 0.75)), OUT, 1.1, 1.5)
    rect(i, 4.5, 6, 23, 4, mix(RED, WHITE, 0.35), OUT, 1.0, 1.0)
    rect(i, 12.5, 3, 7, 3, mix(RED, WHITE, 0.35), OUT, 1.0, 0.8)
    for x in (12, 16, 20):
        line(i, x, 13.5, x, 25.5, "#ffd9d2", 1.3)


@icon("edit-duplicate")
def _(i):
    s = Scene(12, 21)
    box(s, -4, -4, 0, 4, 4, 8, BOX)
    s.render(i)
    s2 = Scene(21, 13)
    box(s2, -4, -4, 0, 4, 4, 8, ORANGE)
    s2.render(i)
    badge(i, 26, 26, "+")


@icon("edit-find")
def _(i):
    circle(i, 13, 13, 8.5, i.radial("#eaf4ff", SKY), OUT, 1.2)
    line(i, 19.5, 19.5, 28, 28, OUT, 5.0)
    line(i, 19.5, 19.5, 28, 28, BROWN, 3.0)


@icon("view-history")
def _(i):
    circle(i, 17, 16, 11.5, i.gradient(WHITE, "#c9ced4"), OUT, 1.1)
    path(i, "M17 9 L17 16 L22 19", stroke=OUT, width=2.0)
    path(i, "M2 10 L5.5 15.5 L9.5 10.5", fill=BLUE, width=1.0)


# ===========================================================================
# Grid, snapping, views, render modes
# ===========================================================================

def _grid_square(i, x, y, size, lines=3):
    rect(i, x, y, size, size, "#2c3137", "#8c949c", 1.0, 1.2)
    for k in range(1, lines + 1):
        t = x + size * k / (lines + 1)
        line(i, t, y + 0.5, t, y + size - 0.5, "#5f666e", 1.0)
        t = y + size * k / (lines + 1)
        line(i, x + 0.5, t, x + size - 0.5, t, "#5f666e", 1.0)


@icon("grid-show")
def _(i):
    _grid_square(i, 3, 3, 26)
    line(i, 3.5, 16, 28.5, 16, AXIS[0], 1.4)
    line(i, 16, 3.5, 16, 28.5, AXIS[1], 1.4)


@icon("grid-larger")
def _(i):
    _grid_square(i, 2, 2, 20, 1)
    badge(i, 24, 24, "+", BLUE, 6)


@icon("grid-smaller")
def _(i):
    _grid_square(i, 2, 2, 20, 3)
    badge(i, 24, 24, "-", BLUE, 6)


@icon("snap-grid")
def _(i):
    _grid_square(i, 2, 16, 14, 1)
    d = "M17 4 L17 14 C17 19 20.5 22.5 24 22.5 C27.5 22.5 31 19 31 14 L31 4 L26.5 4 L26.5 14 C26.5 16 25.5 17.5 24 17.5 C22.5 17.5 21.5 16 21.5 14 L21.5 4 Z"
    path(i, d, fill=i.gradient(mix(RED, WHITE, 0.2), scale(RED, 0.8)), width=1.0)
    rect(i, 17, 4, 4.5, 3.5, "#dfe3e7", OUT, 0.8, 0.3)
    rect(i, 26.5, 4, 4.5, 3.5, "#dfe3e7", OUT, 0.8, 0.3)


@icon("snap-angle")
def _(i):
    line(i, 4, 27, 28, 27, "#dfe3e7", 2.2)
    line(i, 4, 27, 21, 6, "#dfe3e7", 2.2)
    path(i, "M13 27 A9 9 0 0 0 9.8 20", stroke=ORANGE, width=2.2)
    text(i, 22, 22, "°", 12, ORANGE)


@icon("snap-vertex")
def _(i):
    s = Scene(16, 18)
    _wire_cube(i, s, 12, "#8f97a0")
    x, y = s.p(6, 6, 12)
    circle(i, x, y, 4.2, "none", ORANGE, 2.0)
    dot(i, x, y, 1.6, WHITE)


@icon("snap-surface")
def _(i):
    poly(i, [(2, 21), (16, 14), (30, 21), (16, 28)], i.gradient(mix(TEAL, WHITE, 0.3), scale(TEAL, 0.8)), OUT, 1.0)
    arrow(i, 16, 2.5, 16, 19, RED, 2.0, 4.8)


@icon("view-frame")
def _(i):
    for d in ("M3 10 L3 3 L10 3", "M22 3 L29 3 L29 10", "M29 22 L29 29 L22 29", "M10 29 L3 29 L3 22"):
        path(i, d, stroke=OUT, width=4.0)
        path(i, d, stroke=RED, width=2.2)
    s = Scene(16, 18)
    box(s, -4, -4, 0, 4, 4, 8, ORANGE)
    s.render(i)


def _view_icon(i, label, highlight):
    rect(i, 2.5, 4.5, 27, 23, "#1e2226", "#8c949c", 1.0, 2.0)
    rect(i, 2.5, 4.5, 27, 6, "#3a4047", "#8c949c", 1.0, 2.0)
    text(i, 16, 9.6, label, 5.4, WHITE)
    highlight(i)


@icon("view-top")
def _(i):
    _view_icon(i, "TOP", lambda i: (rect(i, 9, 13, 14, 11, "none", BOX, 1.3, 0.5), line(i, 5, 18.5, 27, 18.5, AXIS[0], 1.0), line(i, 16, 11.5, 16, 26, AXIS[1], 1.0)))


@icon("view-front")
def _(i):
    _view_icon(i, "FRONT", lambda i: (rect(i, 9, 13, 14, 11, "none", BOX, 1.3, 0.5), line(i, 5, 24, 27, 24, AXIS[1], 1.0), line(i, 16, 11.5, 16, 26, AXIS[2], 1.0)))


@icon("view-side")
def _(i):
    _view_icon(i, "SIDE", lambda i: (rect(i, 9, 13, 14, 11, "none", BOX, 1.3, 0.5), line(i, 5, 24, 27, 24, AXIS[0], 1.0), line(i, 16, 11.5, 16, 26, AXIS[2], 1.0)))


@icon("view-3d", "view-perspective")
def _(i):
    rect(i, 2.5, 4.5, 27, 23, "#1e2226", "#8c949c", 1.0, 2.0)
    s = Scene(16, 20)
    box(s, -4, -4, 0, 4, 4, 8, BOX)
    s.render(i)
    text(i, 16, 9.6, "3D", 5.4, WHITE)


@icon("view-quad", "layout-quad")
def _(i):
    for x in (2.5, 16.5):
        for y in (3.5, 17):
            rect(i, x, y, 13, 11.5, "#1e2226", "#8c949c", 1.0, 1.2)
    rect(i, 2.5, 3.5, 13, 11.5, "#2d3a47", BLUE, 1.2, 1.2)


# View layouts: every arrangement of one to four panes, the way 3ds Max and
# Blender draw theirs. The window frame holds the panes; pane 1 (the 3D view
# by default) is blue, the 2D views grey with a hint of grid.
def _layout(i, columns, counts):
    x0, y0, w, h, gap = 2.5, 4.5, 27.0, 23.0, 1.6
    rect(i, x0 - 0.5, y0 - 0.5, w + 1.0, h + 1.0, "#14171a", "#8c949c", 1.0, 2.0)
    groups = [c for c in counts if c > 0]
    panes = []
    for gi, count in enumerate(groups):
        if columns:
            gw = (w - gap * (len(groups) + 1)) / len(groups)
            gx = x0 + gap + gi * (gw + gap)
            ph = (h - gap * (count + 1)) / count
            for k in range(count):
                panes.append((gx, y0 + gap + k * (ph + gap), gw, ph))
        else:
            gh = (h - gap * (len(groups) + 1)) / len(groups)
            gy = y0 + gap + gi * (gh + gap)
            pw = (w - gap * (count + 1)) / count
            for k in range(count):
                panes.append((x0 + gap + k * (pw + gap), gy, pw, gh))
    for n, (px, py, pw, ph) in enumerate(panes):
        if n == 0:
            rect(i, px, py, pw, ph, i.gradient(mix(BLUE, WHITE, 0.25), scale(BLUE, 0.7)), "none", 0, 0.8)
            # A horizon: the perspective view at a glance.
            line(i, px + 1.2, py + ph * 0.62, px + pw - 1.2, py + ph * 0.62, mix(BLUE, WHITE, 0.6), 0.8)
        else:
            rect(i, px, py, pw, ph, i.gradient("#a7aeb6", "#7c838b"), "none", 0, 0.8)
            if pw > 5 and ph > 5:
                line(i, px + pw / 2, py + 1.2, px + pw / 2, py + ph - 1.2, "#6b727a", 0.6)
                line(i, px + 1.2, py + ph / 2, px + pw - 1.2, py + ph / 2, "#6b727a", 0.6)


_LAYOUTS = {
    "layout-single": (False, (1, 0)),
    "layout-two-columns": (False, (2, 0)),
    "layout-two-rows": (True, (2, 0)),
    "layout-three-columns": (False, (3, 0)),
    "layout-three-rows": (True, (3, 0)),
    "layout-one-top-two-bottom": (False, (1, 2)),
    "layout-two-top-one-bottom": (False, (2, 1)),
    "layout-one-left-two-right": (True, (1, 2)),
    "layout-two-left-one-right": (True, (2, 1)),
    "layout-four": (False, (2, 2)),
    "layout-one-top-three-bottom": (False, (1, 3)),
    "layout-three-top-one-bottom": (False, (3, 1)),
    "layout-one-left-three-right": (True, (1, 3)),
    "layout-three-left-one-right": (True, (3, 1)),
    "layout-four-columns": (False, (4, 0)),
    "layout-four-rows": (True, (4, 0)),
}

for _name, (_columns, _counts) in _LAYOUTS.items():
    icon(_name)(lambda i, c=_columns, n=_counts: _layout(i, c, n))


@icon("view-maximize")
def _(i):
    rect(i, 3, 3, 26, 26, "#1e2226", "#8c949c", 1.0, 2.0)
    for x1, y1, x2, y2 in ((16, 16, 26, 6), (16, 16, 6, 26)):
        arrow(i, x1 + (x2 - x1) * 0.2, y1 + (y2 - y1) * 0.2, x2, y2, RED, 1.8, 4.4, 4.6)


def _render_mode(i, mode):
    s = Scene(16, 19)
    if mode == "wireframe":
        _wire_cube(i, s, 13, "#dfe3e7", 1.2)
        return
    color = {"flat": BOX, "textured": BOX, "lit": YELLOW}[mode]
    box(s, -6.5, -6.5, 0, 6.5, 6.5, 13, color)
    if mode == "flat":
        s.faces = [(f[0], f[1], f[2], f[3], f[4], True) for f in s.faces]
    s.render(i)
    if mode == "textured":
        for a in range(2):
            for b in range(2):
                if (a + b) % 2 == 0:
                    q = [s.p(-6.5 + a * 6.5, -6.5 + b * 6.5, 13), s.p(a * 6.5, -6.5 + b * 6.5, 13), s.p(a * 6.5, b * 6.5, 13), s.p(-6.5 + a * 6.5, b * 6.5, 13)]
                    poly(i, q, BLUE, OUT, 0.5)
    if mode == "lit":
        path(i, "M26 2 L27.3 5.7 L31 7 L27.3 8.3 L26 12 L24.7 8.3 L21 7 L24.7 5.7 Z", fill=WHITE, width=0.8)


for _mode in ("wireframe", "flat", "textured", "lit"):
    ICONS[f"render-{_mode}"] = (lambda mode: (lambda i: _render_mode(i, mode)))(_mode)


# ===========================================================================
# Groups, visibility, layers
# ===========================================================================

@icon("group-create")
def _(i):
    for cx, cy, color in ((11, 15, ORANGE), (21, 22, BLUE)):
        s = Scene(cx, cy)
        box(s, -3.5, -3.5, 0, 3.5, 3.5, 7, color)
        s.render(i)
    rect(i, 2, 2, 28, 28, "none", ORANGE, 1.4, 2.0)


@icon("group-ungroup")
def _(i):
    for cx, cy, color in ((9, 13, ORANGE), (23, 24, BLUE)):
        s = Scene(cx, cy)
        box(s, -3.5, -3.5, 0, 3.5, 3.5, 7, color)
        s.render(i)
    rect(i, 2, 2, 28, 28, "none", "#8f97a0", 1.2, 2.0)
    line(i, 3, 29, 29, 3, "#8f97a0", 1.2, dash="2 2")


@icon("prefab-create", "asset-prefab")
def _(i):
    s = Scene(16, 19)
    box(s, -6, -6, 0, 0, 6, 6, PURPLE)
    box(s, 0, -6, 0, 6, 0, 12, mix(PURPLE, WHITE, 0.25))
    box(s, 0, 0, 0, 6, 6, 6, scale(PURPLE, 0.9))
    s.render(i)


def _eye(i, open_: bool, slash: bool = False):
    path(i, "M2 16 C6.5 8 25.5 8 30 16 C25.5 24 6.5 24 2 16 Z", fill=i.gradient(WHITE, "#c9ced4") if open_ else "#6f767e", width=1.1)
    if open_:
        circle(i, 16, 16, 5.2, i.radial(SKY, scale(BLUE, 0.7)), OUT, 1.0)
        circle(i, 16, 16, 2.1, OUT, OUT, 0.5)
        circle(i, 14.3, 14.3, 1.0, WHITE, "none", 0)
    if slash:
        line(i, 4.5, 27.5, 27.5, 4.5, OUT, 4.0)
        line(i, 4.5, 27.5, 27.5, 4.5, RED, 2.2)


@icon("hide-selected")
def _(i):
    _eye(i, True, True)


@icon("hide-unselected")
def _(i):
    _eye(i, False, True)
    dot(i, 26, 26, 3.2)


@icon("show-all")
def _(i):
    _eye(i, True)


@icon("isolate")
def _(i):
    _eye(i, True)
    circle(i, 16, 16, 13.5, "none", ORANGE, 1.4)


@icon("layer-new")
def _(i):
    for k, color in enumerate((scale(BLUE, 0.7), BLUE, mix(BLUE, WHITE, 0.3))):
        y = 20 - k * 6
        poly(i, [(3, y), (16, y - 6), (29, y), (16, y + 6)], color, OUT, 1.0)
    badge(i, 25, 25, "+")


@icon("visgroup-new")
def _(i):
    _eye(i, True)
    badge(i, 25, 25, "+")


# ===========================================================================
# Build and run
# ===========================================================================

@icon("map-check")
def _(i):
    rect(i, 5, 3, 20, 26, i.gradient(WHITE, "#c9ced4"), OUT, 1.1, 2.0)
    for y in (9, 15, 21):
        line(i, 13.5, y, 21, y, "#9aa1a9", 1.4)
    polyline(i, [(8, 9), (9.6, 10.8), (12, 7.4)], scale(GREEN, 0.8), 1.6)
    polyline(i, [(8, 15), (9.6, 16.8), (12, 13.4)], scale(GREEN, 0.8), 1.6)
    circle(i, 23, 23, 6, GREEN, OUT, 1.0)
    polyline(i, [(20, 23), (22.3, 25.3), (26, 21)], WHITE, 1.8)


@icon("map-run")
def _(i):
    poly(i, [(7, 3.5), (27.5, 16), (7, 28.5)], i.gradient(mix(GREEN, WHITE, 0.3), scale(GREEN, 0.8)), OUT, 1.2)


@icon("map-stop")
def _(i):
    rect(i, 5.5, 5.5, 21, 21, i.gradient(mix(RED, WHITE, 0.25), scale(RED, 0.8)), OUT, 1.2, 3.0)


@icon("map-compile")
def _(i):
    rect(i, 14.5, 12, 4, 17, i.gradient(mix(BROWN, WHITE, 0.3), BROWN), OUT, 1.0, 1.0)
    path(i, "M5 5.5 L20.5 5.5 L27 9 L27 13 L20.5 14 L5 14 C3.8 14 3 13 3 11.8 L3 7.7 C3 6.5 3.8 5.5 5 5.5 Z",
         fill=i.gradient("#dfe3e7", "#727a83"), width=1.1)


@icon("map-leak")
def _(i):
    s = Scene(16, 20)
    std_box(s, BOX, 12)
    s.render(i)
    path(i, "M22 5 C22 5 17 11 17 14.5 C17 17.3 19.2 19.5 22 19.5 C24.8 19.5 27 17.3 27 14.5 C27 11 22 5 22 5 Z",
         fill=i.gradient(mix(RED, WHITE, 0.3), RED), width=1.0)


# ===========================================================================
# Panels, console, assets, application
# ===========================================================================

@icon("view-console")
def _(i):
    rect(i, 2.5, 4.5, 27, 23, "#0e1012", "#8c949c", 1.1, 2.0)
    rect(i, 2.5, 4.5, 27, 5, "#3a4047", "#8c949c", 1.1, 2.0)
    polyline(i, [(7, 14), (11.5, 17.5), (7, 21)], ORANGE, 2.0)
    line(i, 13.5, 22, 22, 22, "#dfe3e7", 1.8)


@icon("view-output")
def _(i):
    rect(i, 2.5, 4.5, 27, 23, "#0e1012", "#8c949c", 1.1, 2.0)
    for k, (w, color) in enumerate(((18, "#dfe3e7"), (14, "#9aa1a9"), (20, "#dfe3e7"), (10, GREEN))):
        line(i, 6.5, 9.5 + k * 4.5, 6.5 + w, 9.5 + k * 4.5, color, 1.8)


@icon("view-problems", "log-error")
def _(i):
    circle(i, 16, 16, 12, i.gradient(mix(RED, WHITE, 0.2), scale(RED, 0.8)), OUT, 1.1)
    line(i, 11, 11, 21, 21, WHITE, 3.0)
    line(i, 21, 11, 11, 21, WHITE, 3.0)


@icon("log-warning")
def _(i):
    poly(i, [(16, 3), (30, 28), (2, 28)], i.gradient(mix(YELLOW, WHITE, 0.3), scale(YELLOW, 0.85)), OUT, 1.2)
    line(i, 16, 11, 16, 20, OUT, 3.0)
    circle(i, 16, 24.2, 1.8, OUT, OUT, 0.1)


@icon("log-info")
def _(i):
    circle(i, 16, 16, 12, i.gradient(mix(BLUE, WHITE, 0.25), scale(BLUE, 0.8)), OUT, 1.1)
    line(i, 16, 14.5, 16, 23, WHITE, 3.2)
    circle(i, 16, 9.5, 2.0, WHITE, WHITE, 0.1)


@icon("log-clear")
def _(i):
    rect(i, 4, 4.5, 20, 23, "#0e1012", "#8c949c", 1.0, 2.0)
    for y in (10, 15, 20):
        line(i, 8, y, 20, y, "#6f767e", 1.6)
    path(i, "M20 30 L17.5 21 L26 14 L31 19 L24 27.5 Z", fill=PINK, width=1.0)


@icon("view-outliner")
def _(i):
    for y, x, color in ((6, 3, ORANGE), (15.5, 10, BLUE), (25, 10, GREEN)):
        s = Scene(x + 3, y + 3.5, 0.9)
        box(s, -2.5, -2.5, 0, 2.5, 2.5, 5, color)
        s.render(i)
        line(i, x + 9, y, 30, y, "#dfe3e7", 2.0)
    polyline(i, [(6, 11), (6, 25), (9, 25)], "#8f97a0", 1.2)
    line(i, 6, 15.5, 9, 15.5, "#8f97a0", 1.2)


@icon("view-properties")
def _(i):
    rect(i, 3, 3, 26, 26, "#262b31", "#8c949c", 1.0, 2.0)
    for y, x, color in ((9, 18, ORANGE), (16, 11, BLUE), (23, 21, GREEN)):
        line(i, 7, y, 25, y, "#5f666e", 1.8)
        circle(i, x, y, 2.8, color, OUT, 1.0)


@icon("view-tool-properties")
def _(i):
    rect(i, 3, 3, 26, 26, "#262b31", "#8c949c", 1.0, 2.0)
    rect(i, 3, 3, 26, 6, "#3a4047", "#8c949c", 1.0, 2.0)
    for y in (13, 18, 23):
        line(i, 7, y, 12, y, ORANGE, 1.6)
        line(i, 14, y, 25, y, "#9aa1a9", 1.6)


@icon("view-visgroups")
def _(i):
    rect(i, 3, 3, 26, 26, "#262b31", "#8c949c", 1.0, 2.0)
    for y in (9, 16, 23):
        rect(i, 6.5, y - 2.5, 5, 5, GREEN, OUT, 0.9, 0.8)
        polyline(i, [(7.6, y), (8.8, y + 1.2), (10.6, y - 1.4)], WHITE, 1.2)
        line(i, 14, y, 25, y, "#9aa1a9", 1.6)


@icon("view-selection-sets")
def _(i):
    rect(i, 3, 3, 26, 26, "#262b31", "#8c949c", 1.0, 2.0)
    for y, color in ((9, ORANGE), (16, BLUE), (23, PURPLE)):
        rect(i, 6.5, y - 2.5, 5, 5, "none", color, 1.4, 0.8)
        line(i, 14, y, 25, y, "#9aa1a9", 1.6)


@icon("view-assets", "asset-browser")
def _(i):
    for k, (x, y) in enumerate(((3, 3), (17, 3), (3, 17), (17, 17))):
        colors = (ORANGE, BLUE, GREEN, PURPLE)
        rect(i, x, y, 12, 12, i.gradient(mix(colors[k], WHITE, 0.3), scale(colors[k], 0.85)), OUT, 1.0, 1.5)


@icon("asset-material")
def _(i):
    circle(i, 16, 16, 12, i.radial(mix(BLUE, WHITE, 0.6), scale(BLUE, 0.7), 0.35, 0.3), OUT, 1.1)
    circle(i, 12, 11.5, 2.4, WHITE, "none", 0)


@icon("asset-texture")
def _(i):
    rect(i, 3, 3, 26, 26, "#2c3137", "#8c949c", 1.0, 1.5)
    for a in range(4):
        for b in range(4):
            if (a + b) % 2 == 0:
                rect(i, 3 + a * 6.5, 3 + b * 6.5, 6.5, 6.5, ORANGE, "none", 0, 0)
    rect(i, 3, 3, 26, 26, "none", OUT, 1.0, 1.5)


@icon("asset-model", "filter-models")
def _(i):
    path(i, "M8 7 C8 4.5 24 4.5 24 7 L24 25 C24 27.5 8 27.5 8 25 Z", fill=i.gradient(mix(RED, WHITE, 0.25), scale(RED, 0.75)), width=1.1)
    path(i, "M8 7 C8 9.5 24 9.5 24 7", stroke=OUT, width=1.0)
    path(i, "M8 13.5 C8 16 24 16 24 13.5 M8 20 C8 22.5 24 22.5 24 20", stroke=scale(RED, 0.5), width=1.1)


@icon("asset-particle")
def _(i):
    for x, y, r, color in ((10, 20, 5, ORANGE), (19, 12, 4, YELLOW), (23, 22, 3, RED), (13, 9, 2.4, YELLOW), (26, 7, 1.8, ORANGE)):
        circle(i, x, y, r, i.radial(mix(color, WHITE, 0.6), color), OUT, 0.9)


@icon("asset-sound")
def _(i):
    ICONS["entity-sound"](i)


@icon("asset-shader")
def _(i):
    rect(i, 3, 4, 26, 24, "#1e2226", "#8c949c", 1.0, 2.0)
    text(i, 16, 20.5, "{ }", 12, PURPLE)


@icon("asset-folder")
def _(i):
    path(i, "M3 8 L3 26 L29 26 L29 11 L15 11 L12.5 8 Z", fill=i.gradient(mix(YELLOW, WHITE, 0.3), scale(YELLOW, 0.85)), width=1.1)


@icon("asset-map")
def _(i):
    # A folded map sheet with a route: maps (.cymap, .cytilemap).
    poly(i, [(3, 7), (11, 4), (21, 7), (29, 4), (29, 25), (21, 28), (11, 25), (3, 28)], i.gradient("#e9e2c8", "#b7ab84"), OUT, 1.0)
    line(i, 11, 4, 11, 25, "#8f8466", 0.9)
    line(i, 21, 7, 21, 28, "#8f8466", 0.9)
    polyline(i, [(6, 22), (12, 15), (18, 18), (25, 10)], RED, 1.8)
    circle(i, 25, 10, 2.2, RED, OUT, 0.8)


@icon("asset-font")
def _(i):
    rect(i, 3, 4, 26, 24, "#1e2226", "#8c949c", 1.0, 2.0)
    text(i, 13, 22.5, "A", 16, WHITE)
    text(i, 23, 22.5, "a", 11, mix(BLUE, WHITE, 0.3))


@icon("asset-animation")
def _(i):
    # Film strip with a moving dot: clips, skeletons, graphs.
    rect(i, 3, 7, 26, 18, "#2a2f35", OUT, 1.0, 1.5)
    for k in range(6):
        rect(i, 4.6 + k * 4.2, 8.2, 2.2, 2.0, "#cfd4d9", "none", 0, 0.4)
        rect(i, 4.6 + k * 4.2, 21.8, 2.2, 2.0, "#cfd4d9", "none", 0, 0.4)
    for k, a in enumerate((0.25, 0.5, 1.0)):
        circle(i, 10 + k * 6, 16, 2.6, mix("#2a2f35", GREEN, a), "none", 0)


@icon("layout-reset")
def _(i):
    rect(i, 2.5, 4, 27, 24, "#262b31", "#8c949c", 1.1, 1.8)
    rect(i, 2.5, 4, 7, 24, scale(BLUE, 0.7), "#8c949c", 1.1, 1.8)
    rect(i, 22.5, 4, 7, 24, scale(BLUE, 0.7), "#8c949c", 1.1, 1.8)
    curved_arrow(i, 16, 16, 5, 200, 480, RED, 1.6, 3.6)


@icon("settings")
def _(i):
    teeth = []
    for k in range(20):
        a = k * math.pi / 10
        r = 13 if k % 2 == 0 else 10
        teeth.append((16 + math.cos(a) * r, 16 + math.sin(a) * r))
    poly(i, teeth, i.gradient("#e3e6e9", "#6f767e"), OUT, 1.0)
    circle(i, 16, 16, 4.5, "#2a2f35", OUT, 1.0)


@icon("theme-editor")
def _(i):
    path(i, "M16 3 C8.8 3 3 8.4 3 15.3 C3 22.4 8.9 28 16 28 C18.4 28 18.9 26.1 17.8 24.5 C16.5 22.6 17.6 20.3 20 20.3 L23.3 20.3 C26.6 20.3 29 17.9 29 14.9 C29 8.3 23.5 3 16 3 Z",
         fill=i.gradient("#efe0c2", "#b8935c"), width=1.1)
    for (x, y), color in zip(((9, 11.5), (14.5, 7.5), (21, 9.5), (8.8, 18)), (RED, YELLOW, BLUE, GREEN)):
        circle(i, x, y, 2.5, color, OUT, 0.8)


@icon("appearance")
def _(i):
    # A window wearing a palette: swatches over a size slider - themes,
    # colours, and scale, the three parts of the Appearance page.
    rect(i, 2.5, 4, 27, 24, "#262b31", "#8c949c", 1.1, 2.0)
    rect(i, 2.5, 4, 27, 5.5, i.gradient(mix(BLUE, WHITE, 0.2), scale(BLUE, 0.7)), "#8c949c", 1.1, 2.0)
    for k, color in enumerate((RED, YELLOW, GREEN, BLUE)):
        rect(i, 5.2 + k * 5.6, 12.2, 4.6, 4.6, color, OUT, 0.8, 0.8)
    line(i, 6.5, 22.5, 25.5, 22.5, "#8c949c", 1.6)
    line(i, 6.5, 22.5, 17, 22.5, ORANGE, 1.6)
    circle(i, 17, 22.5, 2.7, "#e3e6e9", OUT, 0.9)


@icon("command-repeat")
def _(i):
    # A command line run again: a list row with a looping arrow.
    rect(i, 2.5, 6, 18, 20, "#262b31", "#8c949c", 1.0, 2.0)
    for k, y in enumerate((11, 16, 21)):
        line(i, 6, y, 17, y, "#dfe3e7" if k == 1 else "#8c949c", 1.6)
    curved_arrow(i, 22, 16, 6.5, 150, 470, GREEN, 2.2, 5.0)


@icon("command-history")
def _(i):
    rect(i, 2.5, 4, 20, 24, "#262b31", "#8c949c", 1.0, 2.0)
    for y in (9, 14, 19, 24):
        line(i, 6, y, 18, y, "#8c949c", 1.4)
    circle(i, 23, 22, 6.5, i.gradient(mix(BLUE, WHITE, 0.3), scale(BLUE, 0.75)), OUT, 1.0)
    line(i, 23, 22, 23, 18.5, WHITE, 1.4)
    line(i, 23, 22, 25.5, 23.5, WHITE, 1.4)


@icon("map-info")
def _(i):
    poly(i, [(3, 8), (11, 5), (21, 8), (29, 5), (29, 25), (21, 28), (11, 25), (3, 28)], i.gradient("#e9e2c8", "#b7ab84"), OUT, 1.0)
    circle(i, 16, 16.5, 7.5, i.gradient(mix(BLUE, WHITE, 0.25), scale(BLUE, 0.8)), OUT, 1.0)
    text(i, 16, 21, "i", 11, WHITE)


@icon("go-to")
def _(i):
    # A target with an arrow homing in: go to an object or a position.
    circle(i, 18, 17, 10, "none", RED, 1.8)
    circle(i, 18, 17, 5.5, "none", RED, 1.6)
    circle(i, 18, 17, 1.8, RED, OUT, 0.6)
    arrow(i, 3, 3, 15, 14, BLUE, 2.2, 5.5, 5.0)


@icon("welcome")
def _(i):
    rect(i, 3, 5, 26, 22, "#262b31", "#8c949c", 1.0, 2.0)
    rect(i, 3, 5, 26, 5, i.gradient(mix(ORANGE, WHITE, 0.2), scale(ORANGE, 0.8)), "#8c949c", 1.0, 2.0)
    for k, y in enumerate((14, 18.5, 23)):
        rect(i, 6, y - 2, 4, 3.5, (BLUE, GREEN, YELLOW)[k], OUT, 0.6, 0.6)
        line(i, 12.5, y, 25, y, "#dfe3e7", 1.4)


@icon("command-palette")
def _(i):
    rect(i, 2.5, 6, 27, 20, "#262b31", "#8c949c", 1.0, 2.5)
    polyline(i, [(7, 12), (11, 16), (7, 20)], ORANGE, 2.0)
    line(i, 13.5, 16, 25, 16, "#dfe3e7", 1.8)


@icon("keymap")
def _(i):
    rect(i, 2, 8, 28, 17, i.gradient("#8d949c", "#4f565e"), OUT, 1.1, 2.5)
    for row, y in enumerate((11, 15)):
        for k in range(7):
            rect(i, 4.5 + k * 3.6 + row * 1.2, y, 2.6, 2.6, "#e3e6e9", OUT, 0.5, 0.5)
    rect(i, 9, 19.5, 14, 2.6, "#e3e6e9", OUT, 0.5, 0.5)


@icon("help")
def _(i):
    circle(i, 16, 16, 12.5, i.gradient(mix(BLUE, WHITE, 0.25), scale(BLUE, 0.8)), OUT, 1.1)
    text(i, 16, 21.5, "?", 15, WHITE)


@icon("plugins")
def _(i):
    path(i, "M6 10 L13 10 C13 6.5 19 6.5 19 10 L26 10 L26 17 C29.5 17 29.5 23 26 23 L26 28 L6 28 Z",
         fill=i.gradient(mix(GREEN, WHITE, 0.3), scale(GREEN, 0.8)), width=1.1)


# ===========================================================================
# View filters (Hammer 5's "View:" group)
# ===========================================================================

@icon("filter-world")
def _(i):
    s = Scene(16, 18)
    std_box(s, BOX, 12)
    s.render(i)


@icon("filter-entities")
def _(i):
    ICONS["tool-entity"](i)


@icon("filter-lights")
def _(i):
    for k in range(8):
        a = k * math.pi / 4
        line(i, 16 + math.cos(a) * 9.5, 16 + math.sin(a) * 9.5, 16 + math.cos(a) * 14, 16 + math.sin(a) * 14, OUT, 3.6)
        line(i, 16 + math.cos(a) * 9.5, 16 + math.sin(a) * 9.5, 16 + math.cos(a) * 14, 16 + math.sin(a) * 14, YELLOW, 2.0)
    circle(i, 16, 16, 7, i.radial("#fffbe0", scale(YELLOW, 0.9)), OUT, 1.1)


@icon("filter-props")
def _(i):
    s = Scene(16, 18)
    std_box(s, TAN, 12)
    s.render(i)
    for a, b in (((-6, 6, 0), (6, 6, 12)), ((6, -6, 0), (6, 6, 12))):
        pa, pb = s.p(*a), s.p(*b)
        line(i, pa[0], pa[1], pb[0], pb[1], scale(BROWN, 0.8), 1.0)


@icon("filter-triggers")
def _(i):
    s = Scene(16, 18)
    box(s, -6, -6, 0, 6, 6, 12, ORANGE, opacity=0.55, outline=scale(ORANGE, 1.3))
    s.render(i)


@icon("filter-nodraw", "filter-tool-brushes")
def _(i):
    rect(i, 3, 3, 26, 26, "#efd64a", OUT, 1.0, 1.5)
    for k in range(-3, 5):
        line(i, 3 + k * 7, 29, 3 + k * 7 + 26, 3, "#2a2f35", 2.6)
    rect(i, 3, 3, 26, 26, "none", OUT, 1.1, 1.5)


@icon("filter-sky")
def _(i):
    rect(i, 3, 3, 26, 26, i.gradient("#9dd7ff", "#2a6fc0"), OUT, 1.0, 2.0)
    path(i, "M7 20 C7 17 10 15.8 12 17 C13 14 18 13.8 19.3 17.3 C22 16.5 25 18 25 20.5 C25 22.6 23.4 23.7 21.6 23.7 L10 23.7 C8.2 23.7 7 22.4 7 20 Z",
         fill=WHITE, width=0.9)


@icon("filter-paths")
def _(i):
    ICONS["tool-path"](i)


@icon("filter-decals")
def _(i):
    ICONS["tool-decal"](i)


@icon("filter-sound")
def _(i):
    ICONS["entity-sound"](i)


@icon("filter-navigation")
def _(i):
    ICONS["select-navigation"](i)


# ---------------------------------------------------------------------------
# Hammer tone: every colour is muted and nothing glares, so an icon reads as
# grey metal with, at most, a small accent. Applied to the finished SVG, it
# covers the palette and any literal colour an icon uses.
# ---------------------------------------------------------------------------
TONE_SATURATION = 0.34   # Fraction of each colour's saturation kept.
TONE_SEMANTIC_SATURATION = 0.8  # Run, stop, build, and diagnostics: the colour is the message.
TONE_MAX_LIGHTNESS = 0.80
TONE_KEEP = {ORANGE}     # The accent survives untouched.
SEMANTIC_ICONS = {"map-run", "map-stop", "map-compile", "map-check", "map-leak", "log-error", "log-warning", "log-info"}
_HEX = re.compile(r"#([0-9a-fA-F]{6})\b")


def tone(color: str, saturation: float = TONE_SATURATION) -> str:
    if color.lower() in TONE_KEEP:
        return color
    r_, g_, b_ = (v / 255.0 for v in rgb(color))
    h, l, s = colorsys.rgb_to_hls(r_, g_, b_)
    s *= saturation
    l = min(l, TONE_MAX_LIGHTNESS)
    return hexc(*(v * 255.0 for v in colorsys.hls_to_rgb(h, l, s)))


def tone_svg(svg: str, name: str = "") -> str:
    saturation = TONE_SEMANTIC_SATURATION if name in SEMANTIC_ICONS else TONE_SATURATION
    return _HEX.sub(lambda m: tone("#" + m.group(1), saturation), svg)


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def render(out_dir: Path) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("*.svg"):
        old.unlink()  # Renamed or removed icons leave no stale files.
    written = []
    for name, draw in ICONS.items():
        if name.endswith("-mini"):
            continue
        icon_ = Icon()
        draw(icon_)
        target = out_dir / f"{name}.svg"
        target.write_text(tone_svg(icon_.svg(), name), encoding="utf-8")
        written.append(target)
    return written


def export_png(svgs: list[Path], png_dir: Path, sizes: tuple[int, ...]) -> None:
    converter = shutil.which("rsvg-convert")
    if converter is None:
        print("rsvg-convert not found; PNG export skipped")
        return
    for size in sizes:
        target_dir = png_dir / str(size)
        target_dir.mkdir(parents=True, exist_ok=True)
        for old in target_dir.glob("*.png"):
            old.unlink()
        for svg in svgs:
            subprocess.run([converter, "-w", str(size), "-h", str(size), "-o", str(target_dir / f"{svg.stem}.png"), str(svg)], check=True)


def contact_sheet(png_dir: Path, sheet: Path, size: int = 64) -> None:
    magick = shutil.which("magick")
    source = png_dir / str(size)
    if magick is None or not source.is_dir():
        return
    pngs = sorted(str(p) for p in source.glob("*.png"))
    fonts = [f for f in ("/System/Library/Fonts/Supplemental/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc",
                         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf") if Path(f).is_file()]
    font = ["-font", fonts[0]] if fonts else []
    result = subprocess.run(
        [magick, "montage", *font, "-label", "%t", *pngs, "-background", "#2b2d31", "-fill", "#c9ced4", "-pointsize", "10",
         "-tile", "12x", "-geometry", f"{size}x{size}+18+10", str(sheet)],
        check=False,
    )
    if result.returncode != 0:
        print("contact sheet not written (ImageMagick failed); icons and PNGs are unaffected")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate the editor colour icon set.")
    root = Path(__file__).resolve().parents[2]
    parser.add_argument("--out", type=Path, default=root / "src/CypherEditor/Resources/Icons/cypher")
    parser.add_argument("--png", type=Path, default=None, help="PNG export folder (default: <out>/png)")
    parser.add_argument("--sheet", type=Path, default=None, help="Contact sheet path (default: <png>/contact_sheet.png)")
    args = parser.parse_args()
    svgs = render(args.out)
    png_dir = args.png or args.out / "png"
    export_png(svgs, png_dir, (24, 32, 64))
    for stale in (png_dir / "48",):
        if stale.is_dir():
            shutil.rmtree(stale)
    contact_sheet(png_dir, args.sheet or png_dir / "contact_sheet.png")
    print(f"{len(svgs)} icons written to {args.out}")


if __name__ == "__main__":
    main()
