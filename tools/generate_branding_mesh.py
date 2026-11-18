#!/usr/bin/env python3
"""Generate the texture-free sidebar mesh from the checked-in icon SVGs.

Run: python tools/generate_branding_mesh.py [--check] [--preview-svg mesh.svg]
Generation only: python -m pip install numpy==2.5.4 mapbox-earcut==2.1.0
Runtime needs neither Python, SVG parsing, image files, nor a logo texture.
The intentionally small SVG subset is rejected explicitly if assets change.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import math
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

try:
    import mapbox_earcut
    import numpy as np
except ImportError as exc:
    raise SystemExit("Generation requires: python -m pip install numpy mapbox-earcut") from exc

ROOT = Path(__file__).resolve().parents[1]
LIGHT = ROOT / "assets/branding/gakumotion-icon-light.svg"
DARK = ROOT / "assets/branding/gakumotion-icon-dark.svg"
OUTPUT = ROOT / "src/gui/generated/GkGuiBrandingMesh.hpp"
TOLERANCE = 0.02  # SVG units: less than 0.014 screen pixels at sidebar width 115.
TOKEN = re.compile(r"[A-Za-z]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")


def cross(a, b):
    return a[0] * b[1] - a[1] * b[0]


def area(points):
    return sum(cross(a, b) for a, b in zip(points, points[1:] + points[:1])) * 0.5


def clean(points):
    out = []
    for p in points:
        p = np.asarray(p, dtype=float)
        if not out or np.linalg.norm(p - out[-1]) > 1e-6:
            out.append(p)
    if len(out) > 1 and np.linalg.norm(out[-1] - out[0]) < 1e-6:
        out.pop()
    return out


def flatten_curve(control):
    """Subdivision preserves endpoints and bounds geometric error, not texture."""
    start, end = control[0], control[-1]
    chord = end - start
    length = np.linalg.norm(chord)
    error = max((abs(cross(p - start, chord)) / length if length else np.linalg.norm(p - start))
                for p in control[1:-1])
    if error <= TOLERANCE:
        return [end]
    levels = [list(control)]
    while len(levels[-1]) > 1:
        levels.append([(a + b) * 0.5 for a, b in zip(levels[-1], levels[-1][1:])])
    left = [level[0] for level in levels]
    right = [level[-1] for level in reversed(levels)]
    return flatten_curve(left) + flatten_curve(right)


def parse_path(data):
    tokens = TOKEN.findall(data)
    # No silently ignored syntax, relative commands, arcs, or transforms.
    if re.sub(r"[\s,]", "", TOKEN.sub("", data)):
        raise ValueError("Unsupported SVG path syntax")
    position = np.zeros(2)
    contours, points = [], []
    i, command = 0, None
    counts = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "Q": 4}
    while i < len(tokens):
        if tokens[i].isalpha():
            command = tokens[i]
            i += 1
            if command == "Z":
                if points:
                    contours.append((clean(points), True))
                    position = points[0]
                    points = []
                command = None
                continue
        if command not in counts:
            raise ValueError(f"Unsupported SVG path command: {command}")
        count = counts[command]
        values = np.array([float(v) for v in tokens[i:i + count]])
        if len(values) != count:
            raise ValueError("Incomplete SVG path command")
        i += count
        if command == "M":
            if points:
                contours.append((clean(points), False))
            position = values
            points = [position]
            command = "L"
        elif command == "L":
            position = values
            points.append(position)
        elif command == "H":
            position = np.array([values[0], position[1]])
            points.append(position)
        elif command == "V":
            position = np.array([position[0], values[0]])
            points.append(position)
        else:
            control = [position] + list(values.reshape(-1, 2))
            points.extend(flatten_curve(control))
            position = control[-1]
    if points:
        contours.append((clean(points), False))
    return contours


def circle(cx, cy, radius):
    count = max(12, math.ceil(math.pi / math.acos(max(-1.0, 1 - TOLERANCE / radius))))
    return [np.array([cx + radius * math.cos(t), cy + radius * math.sin(t)])
            for t in np.linspace(0, 2 * math.pi, count, endpoint=False)]


def rectangle(element):
    x, y = float(element.get("x", 0)), float(element.get("y", 0))
    width, height = float(element.get("width")), float(element.get("height"))
    rx = min(float(element.get("rx", 0)), width / 2)
    ry = min(float(element.get("ry", rx)), height / 2)
    if not rx or not ry:
        return [np.array(p) for p in [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]]
    out = []
    steps = max(2, math.ceil(math.pi / 2 / math.acos(max(-1.0, 1 - TOLERANCE / max(rx, ry)))))
    for cx, cy, start in [(x + width - rx, y + ry, -math.pi / 2),
                          (x + width - rx, y + height - ry, 0),
                          (x + rx, y + height - ry, math.pi / 2), (x + rx, y + ry, math.pi)]:
        for t in np.linspace(start, start + math.pi / 2, steps + 1):
            out.append(np.array([cx + rx * math.cos(t), cy + ry * math.sin(t)]))
    return clean(out)


def contains(ring, point):
    inside = False
    x, y = point
    for a, b in zip(ring, ring[1:] + ring[:1]):
        if (a[1] > y) != (b[1] > y) and x < (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]:
            inside = not inside
    return inside


def rgb(color):
    if not re.fullmatch(r"#[0-9a-fA-F]{6}", color):
        raise ValueError(f"Unsupported SVG color: {color}")
    return tuple(int(color[i:i + 2], 16) for i in (1, 3, 5))


@dataclass(frozen=True)
class Paint:
    stops: tuple
    start: tuple = (0.0, 0.0)
    end: tuple = (0.0, 0.0)

    def parameter(self, point):
        axis = np.array(self.end) - self.start
        return float((point - self.start) @ axis / (axis @ axis))

    def sample(self, point):
        if len(self.stops) == 1:
            return self.stops[0][1]
        value = self.parameter(point)
        if value <= self.stops[0][0]:
            return self.stops[0][1]
        for (a, ca), (b, cb) in zip(self.stops, self.stops[1:]):
            if value <= b:
                ratio = (value - a) / (b - a)
                return tuple(round(x + (y - x) * ratio) for x, y in zip(ca, cb))
        return self.stops[-1][1]


def clip_gradient(ring, paint, lower, upper):
    """Split at SVG stop planes so every triangle has a linear color field."""
    result = ring
    for bound, sign in ((lower, 1), (upper, -1)):
        if not math.isfinite(bound):
            continue
        output = []
        for a, b in zip(result, result[1:] + result[:1]):
            va, vb = paint.parameter(a), paint.parameter(b)
            ina, inb = sign * (va - bound) >= -1e-10, sign * (vb - bound) >= -1e-10
            if ina:
                output.append(a)
            if ina != inb:
                output.append(a + (b - a) * ((bound - va) / (vb - va)))
        result = clean(output)
        if len(result) < 3:
            return []
    return result


def boundary_attributes(point, rings):
    """New stop-plane vertices inherit the actual silhouette normal, not the cut."""
    best = math.inf
    result = None
    for ring in rings:
        vectors = normals(ring)
        for i, (a, b) in enumerate(zip(ring, ring[1:] + ring[:1])):
            edge = b - a
            t = float(np.clip((point - a) @ edge / (edge @ edge), 0, 1))
            distance = float(np.linalg.norm(point - (a + t * edge)))
            if distance < best:
                best = distance
                result = vectors[i] * (1 - t) + vectors[(i + 1) % len(ring)] * t
    return best, result


def normals(ring):
    edges = [b - a for a, b in zip(ring, ring[1:] + ring[:1])]
    ns = [np.array([d[1], -d[0]]) / np.linalg.norm(d) for d in edges]
    result = []
    for i, n in enumerate(ns):
        mean = (ns[i - 1] + n) * 0.5
        length2 = float(mean @ mean)
        # Bound sharp cusp miters. No unbounded star-tip fringe spikes.
        mean *= min(1 / length2, 4 / math.sqrt(length2)) if length2 > 1e-8 else 0
        result.append(mean)
    return result


def stroke_ring(points, width, closed, linecap):
    """Offset sampled curves, including the icon's actual round line caps."""
    if closed:
        ns = normals(points)
    else:
        directions = [b - a for a, b in zip(points, points[1:])]
        ns = [np.array([d[1], -d[0]]) / np.linalg.norm(d) for d in directions]
        ns = [ns[0]] + [(a + b) / max(1e-8, 1 + a @ b) for a, b in zip(ns, ns[1:])] + [ns[-1]]
    left = [p + n * width / 2 for p, n in zip(points, ns)]
    right = [p - n * width / 2 for p, n in zip(points, ns)]
    if closed:
        return [left, list(reversed(right))]
    if linecap == "butt":
        return [left + list(reversed(right))]
    if linecap != "round":
        raise ValueError(f"Unsupported stroke-linecap: {linecap}")
    steps = max(3, math.ceil(math.pi / math.acos(max(-1.0, 1 - TOLERANCE / (width / 2)))))
    def cap(center, normal):
        angle = math.atan2(normal[1], normal[0])
        return [center + np.array([math.cos(t), math.sin(t)]) * width / 2
                for t in np.linspace(angle, angle + math.pi, steps + 1)]
    return [clean(left + cap(points[-1], ns[-1]) + list(reversed(right)) + cap(points[0], -ns[0]))]


def read_shapes(path):
    shapes = []
    root = ET.parse(path).getroot()
    paints = {}
    for element in root.iter():
        if element.tag.rsplit("}", 1)[-1] != "linearGradient":
            continue
        if element.get("gradientUnits") != "userSpaceOnUse":
            raise ValueError("Only userSpaceOnUse linear gradients are supported")
        stops = tuple((float(stop.get("offset", 0)), rgb(stop.get("stop-color"))) for stop in element)
        if len(stops) < 2 or any(a[0] >= b[0] for a, b in zip(stops, stops[1:])):
            raise ValueError("Gradient stops must increase strictly")
        paints[element.get("id")] = Paint(stops, (float(element.get("x1")), float(element.get("y1"))),
                                         (float(element.get("x2")), float(element.get("y2"))))
    def paint(value):
        match = re.fullmatch(r"url\(#([^)]*)\)", value)
        return paints[match[1]] if match else Paint(((0.0, rgb(value)),))
    def visit(element, inherited):
        tag = element.tag.rsplit("}", 1)[-1]
        if "transform" in element.attrib or "style" in element.attrib:
            raise ValueError("Branding generator requires explicit, untransformed SVG attributes")
        style = dict(inherited)
        for key in ("fill", "stroke", "stroke-width", "stroke-linecap", "fill-rule"):
            if key in element.attrib:
                style[key] = element.get(key)
        if tag in ("svg", "g"):
            for child in element:
                visit(child, style)
            return
        if tag in ("title", "desc", "defs"):
            return
        if tag == "path":
            contours = parse_path(element.get("d", ""))
        elif tag == "circle":
            contours = [(circle(float(element.get("cx")), float(element.get("cy")), float(element.get("r"))), True)]
        elif tag == "rect":
            contours = [(rectangle(element), True)]
        else:
            raise ValueError(f"Unsupported SVG element: {tag}")
        name = element.get("id", tag)
        if style["fill"] != "none":
            shapes.append((name, paint(style["fill"]), [p for p, _ in contours]))
        if style["stroke"] != "none":
            width = float(style["stroke-width"])
            if tag == "circle":
                cx, cy, radius = float(element.get("cx")), float(element.get("cy")), float(element.get("r"))
                rings = [circle(cx, cy, radius + width / 2), circle(cx, cy, radius - width / 2)]
            else:
                rings = [r for points, closed in contours for r in stroke_ring(points, width, closed, style["stroke-linecap"])]
            shapes.append((name + "-stroke", paint(style["stroke"]), rings))
    visit(root, {"fill": "#000000", "stroke": "none", "stroke-width": "1", "stroke-linecap": "butt", "fill-rule": "nonzero"})
    return shapes


def triangulate(shapes):
    parts = []
    expanded = []
    for name, color, rings in shapes:
        gradient = next((paint for paint in color if len(paint.stops) > 1), None)
        if gradient:
            if len(rings) != 1 or any(paint.start != gradient.start or paint.end != gradient.end
                                     for paint in color if len(paint.stops) > 1):
                raise ValueError("Gradient splitting requires one contour and a shared Light/Dark gradient axis")
            cuts = sorted({stop[0] for paint in color if len(paint.stops) > 1 for stop in paint.stops})
            bounds = [-math.inf] + cuts + [math.inf]
            for j, (lower, upper) in enumerate(zip(bounds, bounds[1:])):
                clipped = clip_gradient(rings[0], gradient, lower, upper)
                if clipped and abs(area(clipped)) > 1e-8:
                    expanded.append((f"{name}-gradient-{j}", color, [clipped], rings))
        else:
            expanded.append((name, color, rings, None))
    for name, color, rings, original in expanded:
        rings = [clean(r) for r in rings]
        # Containment, not authoring order/winding, establishes real transparent holes.
        depths = [sum(contains(other, ring[0]) for j, other in enumerate(rings) if i != j)
                  for i, ring in enumerate(rings)]
        for i, outer in enumerate(rings):
            if depths[i] % 2:
                continue
            group = [outer] + [r for j, r in enumerate(rings)
                               if depths[j] == depths[i] + 1 and contains(outer, r[0])]
            group = [r if (area(r) > 0) == (j == 0) else list(reversed(r)) for j, r in enumerate(group)]
            points = [p for r in group for p in r]
            indices = mapbox_earcut.triangulate_float64(np.array(points), np.cumsum([len(r) for r in group], dtype=np.uint32))
            expected = sum(area(r) for r in group)
            actual = sum(abs(cross(points[b] - points[a], points[c] - points[a])) / 2 for a, b, c in indices.reshape(-1, 3))
            if abs(actual - expected) > max(1e-5, expected * 1e-8):
                raise ValueError(f"Triangulation area mismatch for {name}: {actual} vs {expected}")
            edges, vectors, widths = [], [], []
            offset = 0
            for ring in group:
                for j, (a, b) in enumerate(zip(ring, ring[1:] + ring[:1])):
                    # Stop-plane cuts are internal, never AA silhouette edges.
                    if original is None or boundary_attributes((a + b) * 0.5, original)[0] < 1e-7:
                        edges.append((offset + j, offset + (j + 1) % len(ring)))
                vectors.extend(normals(ring) if original is None else [boundary_attributes(p, original)[1] for p in ring])
                offset += len(ring)
            # Distance across the filled shape limits inward AA displacement and
            # attenuates subpixel strokes/tips instead of inverting narrow strips.
            for i, (point, normal) in enumerate(zip(points, vectors)):
                direction = -normal
                distance = math.inf
                reference = original if original is not None else group
                for a, b in ((a, b) for ring in reference for a, b in zip(ring, ring[1:] + ring[:1])):
                    if np.linalg.norm(point - a) < 1e-7 or np.linalg.norm(point - b) < 1e-7:
                        continue
                    segment = b - a
                    denominator = cross(direction, segment)
                    if abs(denominator) < 1e-10:
                        continue
                    t = cross(a - point, segment) / denominator
                    u = cross(a - point, direction) / denominator
                    if t > 1e-5 and -1e-7 <= u <= 1 + 1e-7:
                        distance = min(distance, t)
                widths.append(min(distance, 10000.0))
            parts.append((name, color, points, vectors, widths, indices.tolist(), edges))
    return parts


def f(value):
    value = round(float(value), 5)
    text = f"{value:.5f}".rstrip("0").rstrip(".") if value else "0"
    return text + ("f" if "." in text else ".0f")


def generate():
    light, dark = read_shapes(LIGHT), read_shapes(DARK)
    if len(light) != len(dark) or any(a[0] != b[0].replace("gm-icon-dark-", "gm-icon-") or
            len(a[2]) != len(b[2]) or
            any(not np.array_equal(x, y) for x, y in zip(a[2], b[2])) for a, b in zip(light, dark)):
        raise ValueError("Light/Dark icons must share geometry")
    parts = triangulate([(a[0], (a[1], b[1]), a[2]) for a, b in zip(light, dark)])
    all_points = np.array([p for part in parts for p in part[2]])
    minimum, maximum = all_points.min(axis=0), all_points.max(axis=0)
    vertices, indices, edges, ranges = [], [], [], []
    for name, color, points, vectors, widths, triangles, boundary in parts:
        if len(points) * 2 >= 65536:
            raise ValueError("Branding part exceeds a 16-bit ImGui vertex batch")
        ranges.append((len(vertices), len(points), len(indices), len(triangles), len(edges), len(boundary), name))
        vertices.extend(tuple(p - minimum) + tuple(n) + (width,) + (color[0].sample(p), color[1].sample(p))
                        for p, n, width in zip(points, vectors, widths))
        indices.extend(triangles)
        edges.extend(boundary)
    lines = ["#pragma once", "// GENERATED by tools/generate_branding_mesh.py. Do not edit.",
             "// Source SHA-256 (LF-normalized Light/Dark): " + hashlib.sha256(LIGHT.read_text(encoding="utf8").encode("utf8")).hexdigest(),
             "//                                         " + hashlib.sha256(DARK.read_text(encoding="utf8").encode("utf8")).hexdigest(),
             "#include <cstdint>", "", "namespace GakuMotion::GuiBranding::Mesh {",
             "struct Rgb { std::uint8_t r, g, b; };",
             "struct Vertex { float x, y, nx, ny, width; Rgb light, dark; };",
             "struct Edge { std::uint16_t a, b; };",
             "struct Part { int vertexOffset, vertexCount, indexOffset, indexCount, edgeOffset, edgeCount; };",
             f"inline constexpr float Width = {f(maximum[0] - minimum[0])};",
             f"inline constexpr float Height = {f(maximum[1] - minimum[1])};",
             "inline constexpr Vertex Vertices[] = {"]
    lines.extend("    {" + ", ".join(f(v) for v in row[:5]) + ", {" + ", ".join(map(str, row[5])) +
                 "}, {" + ", ".join(map(str, row[6])) + "}}," for row in vertices)
    lines += ["};", "inline constexpr std::uint16_t Indices[] = {"]
    lines.extend("    " + ", ".join(str(v) for v in indices[i:i + 18]) + "," for i in range(0, len(indices), 18))
    lines += ["};", "inline constexpr Edge Edges[] = {"]
    lines.extend("    {" + str(a) + ", " + str(b) + "}," for a, b in edges)
    lines += ["};", "inline constexpr Part Parts[] = {"]
    lines.extend("    {" + ", ".join(str(v) for v in row[:6]) + "}, // " + row[6] for row in ranges)
    lines += ["};", "} // namespace GakuMotion::GuiBranding::Mesh", ""]
    return "\n".join(lines), parts, minimum, maximum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail if the generated header is stale")
    parser.add_argument("--preview-svg", type=Path, help="Write an opaque triangle preview for offline inspection")
    args = parser.parse_args()
    generated, parts, minimum, maximum = generate()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf8") != generated:
            raise SystemExit("Branding mesh is stale. Run python tools/generate_branding_mesh.py")
    else:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        OUTPUT.write_text(generated, encoding="utf8", newline="\n")
    if args.preview_svg:
        lines = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{minimum[0]} {minimum[1]} {maximum[0]-minimum[0]} {maximum[1]-minimum[1]}">']
        for part, (_, color, points, _, _, triangles, _) in enumerate(parts):
            paint = color[0]
            if len(paint.stops) > 1:
                lines.append(f'<defs><linearGradient id="gradient-{part}" gradientUnits="userSpaceOnUse" x1="{paint.start[0]}" y1="{paint.start[1]}" x2="{paint.end[0]}" y2="{paint.end[1]}">')
                lines.extend(f'<stop offset="{offset}" stop-color="#{r:02x}{g:02x}{b:02x}"/>' for offset, (r, g, b) in paint.stops)
                lines.append('</linearGradient></defs>')
                fill = f'url(#gradient-{part})'
            else:
                fill = '#%02x%02x%02x' % paint.stops[0][1]
            for a, b, c in np.array(triangles).reshape(-1, 3):
                coordinates = " ".join(f"{points[i][0]},{points[i][1]}" for i in (a, b, c))
                lines.append(f'<polygon fill="{fill}" points="{coordinates}"/>')
        args.preview_svg.write_text("\n".join(lines + ["</svg>"]), encoding="utf8")
    print(f"Branding mesh {'verified' if args.check else 'generated'}: {len(parts)} parts, "
          f"{sum(len(p[2]) for p in parts)} vertices, {sum(len(p[5]) // 3 for p in parts)} triangles; "
          f"content bounds {minimum.tolist()} .. {maximum.tolist()}")


if __name__ == "__main__":
    main()
