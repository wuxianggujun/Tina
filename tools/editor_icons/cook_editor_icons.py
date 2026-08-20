#!/usr/bin/env python3
"""Cook the committed Tina Editor Lucide SVG set into a deterministic alpha atlas."""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

try:
    from PIL import Image, ImageDraw
except ImportError as error:
    raise SystemExit(
        "Pillow is required only when recooking Editor icons; install it with "
        "'python -m pip install Pillow'."
    ) from error


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MANIFEST_PATH = REPOSITORY_ROOT / "resources/editor/icons/manifest.json"
GENERATED_HEADER_PATH = (
    REPOSITORY_ROOT / "src/editor_app/EditorIconAtlas.generated.hpp"
)
GENERATED_ALPHA_PATH = (
    REPOSITORY_ROOT / "src/editor_app/EditorIconAtlas.generated.inc"
)
NUMBER_PATTERN = r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?"
PATH_TOKEN_PATTERN = re.compile(rf"[A-Za-z]|{NUMBER_PATTERN}")
ENUM_NAME_PATTERN = re.compile(r"[A-Z][A-Za-z0-9]*\Z")
ALLOWED_ROOT_ATTRIBUTES = {
    "class",
    "width",
    "height",
    "viewBox",
    "fill",
    "stroke",
    "stroke-width",
    "stroke-linecap",
    "stroke-linejoin",
}
ALLOWED_ELEMENT_ATTRIBUTES = {
    "d",
    "x",
    "y",
    "x1",
    "y1",
    "x2",
    "y2",
    "cx",
    "cy",
    "r",
    "rx",
    "ry",
    "width",
    "height",
    "points",
}
ALLOWED_ELEMENTS = {"path", "line", "circle", "rect", "polyline", "polygon"}
COMMAND_PARAMETER_COUNTS = {
    "M": 2,
    "L": 2,
    "H": 1,
    "V": 1,
    "C": 6,
    "S": 4,
    "Q": 4,
    "T": 2,
    "A": 7,
}


@dataclass(frozen=True)
class IconSpec:
    name: str
    source: Path


@dataclass
class PathState:
    current: tuple[float, float] = (0.0, 0.0)
    start: tuple[float, float] = (0.0, 0.0)
    last_cubic_control: tuple[float, float] | None = None
    last_quadratic_control: tuple[float, float] | None = None


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def parse_number(value: str, context: str) -> float:
    if re.fullmatch(NUMBER_PATTERN, value) is None:
        raise ValueError(f"{context} must be a finite SVG number")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{context} must be finite")
    return result


def add_points(
    left: tuple[float, float], right: tuple[float, float]
) -> tuple[float, float]:
    return left[0] + right[0], left[1] + right[1]


def reflect(
    control: tuple[float, float] | None, around: tuple[float, float]
) -> tuple[float, float]:
    if control is None:
        return around
    return 2.0 * around[0] - control[0], 2.0 * around[1] - control[1]


def quadratic_points(
    start: tuple[float, float], control: tuple[float, float],
    end: tuple[float, float], steps: int = 12,
) -> Iterable[tuple[float, float]]:
    for step in range(1, steps + 1):
        t = step / steps
        one_minus_t = 1.0 - t
        yield (
            one_minus_t * one_minus_t * start[0]
            + 2.0 * one_minus_t * t * control[0]
            + t * t * end[0],
            one_minus_t * one_minus_t * start[1]
            + 2.0 * one_minus_t * t * control[1]
            + t * t * end[1],
        )


def cubic_points(
    start: tuple[float, float], first: tuple[float, float],
    second: tuple[float, float], end: tuple[float, float], steps: int = 16,
) -> Iterable[tuple[float, float]]:
    for step in range(1, steps + 1):
        t = step / steps
        one_minus_t = 1.0 - t
        yield (
            one_minus_t**3 * start[0]
            + 3.0 * one_minus_t**2 * t * first[0]
            + 3.0 * one_minus_t * t**2 * second[0]
            + t**3 * end[0],
            one_minus_t**3 * start[1]
            + 3.0 * one_minus_t**2 * t * first[1]
            + 3.0 * one_minus_t * t**2 * second[1]
            + t**3 * end[1],
        )


def arc_points(
    start: tuple[float, float], radii: tuple[float, float], rotation_degrees: float,
    large_arc: bool, sweep: bool, end: tuple[float, float],
) -> Iterable[tuple[float, float]]:
    rx, ry = abs(radii[0]), abs(radii[1])
    if rx == 0.0 or ry == 0.0 or start == end:
        yield end
        return

    phi = math.radians(rotation_degrees % 360.0)
    cos_phi = math.cos(phi)
    sin_phi = math.sin(phi)
    delta_x = (start[0] - end[0]) * 0.5
    delta_y = (start[1] - end[1]) * 0.5
    x_prime = cos_phi * delta_x + sin_phi * delta_y
    y_prime = -sin_phi * delta_x + cos_phi * delta_y
    radii_scale = x_prime * x_prime / (rx * rx) + y_prime * y_prime / (ry * ry)
    if radii_scale > 1.0:
        scale = math.sqrt(radii_scale)
        rx *= scale
        ry *= scale

    numerator = max(
        0.0,
        rx * rx * ry * ry
        - rx * rx * y_prime * y_prime
        - ry * ry * x_prime * x_prime,
    )
    denominator = rx * rx * y_prime * y_prime + ry * ry * x_prime * x_prime
    center_scale = 0.0 if denominator == 0.0 else math.sqrt(numerator / denominator)
    if large_arc == sweep:
        center_scale = -center_scale
    center_x_prime = center_scale * rx * y_prime / ry
    center_y_prime = -center_scale * ry * x_prime / rx
    center_x = (
        cos_phi * center_x_prime
        - sin_phi * center_y_prime
        + (start[0] + end[0]) * 0.5
    )
    center_y = (
        sin_phi * center_x_prime
        + cos_phi * center_y_prime
        + (start[1] + end[1]) * 0.5
    )

    def vector_angle(left: tuple[float, float], right: tuple[float, float]) -> float:
        cross = left[0] * right[1] - left[1] * right[0]
        dot = left[0] * right[0] + left[1] * right[1]
        return math.atan2(cross, dot)

    unit_start = (
        (x_prime - center_x_prime) / rx,
        (y_prime - center_y_prime) / ry,
    )
    unit_end = (
        (-x_prime - center_x_prime) / rx,
        (-y_prime - center_y_prime) / ry,
    )
    start_angle = vector_angle((1.0, 0.0), unit_start)
    delta_angle = vector_angle(unit_start, unit_end)
    if not sweep and delta_angle > 0.0:
        delta_angle -= 2.0 * math.pi
    elif sweep and delta_angle < 0.0:
        delta_angle += 2.0 * math.pi
    steps = max(4, math.ceil(abs(delta_angle) / (math.pi / 24.0)))
    for step in range(1, steps + 1):
        angle = start_angle + delta_angle * step / steps
        x = rx * math.cos(angle)
        y = ry * math.sin(angle)
        yield (
            cos_phi * x - sin_phi * y + center_x,
            sin_phi * x + cos_phi * y + center_y,
        )


def parse_path(data: str) -> list[tuple[list[tuple[float, float]], bool]]:
    tokens = PATH_TOKEN_PATTERN.findall(data.replace(",", " "))
    if not tokens:
        raise ValueError("SVG path data must not be empty")
    compact_source = re.sub(r"[\s,]", "", data)
    if "".join(tokens) != compact_source:
        raise ValueError(f"unsupported SVG path token in {data!r}")

    paths: list[tuple[list[tuple[float, float]], bool]] = []
    points: list[tuple[float, float]] = []
    state = PathState()
    index = 0
    command: str | None = None

    def finish(closed: bool = False) -> None:
        nonlocal points
        if points:
            paths.append((points, closed))
            points = []

    def absolute_point(x: float, y: float, relative: bool) -> tuple[float, float]:
        point = (x, y)
        return add_points(state.current, point) if relative else point

    while index < len(tokens):
        if tokens[index].isalpha():
            command = tokens[index]
            index += 1
            if command.upper() == "Z":
                if not points:
                    raise ValueError("SVG close-path command has no open subpath")
                state.current = state.start
                state.last_cubic_control = None
                state.last_quadratic_control = None
                finish(True)
                command = None
                continue
        if command is None or command.upper() not in COMMAND_PARAMETER_COUNTS:
            raise ValueError(f"unsupported or missing SVG path command near token {index}")
        upper = command.upper()
        count = COMMAND_PARAMETER_COUNTS[upper]
        if index + count > len(tokens) or any(
            token.isalpha() for token in tokens[index : index + count]
        ):
            raise ValueError(f"SVG path command {command} has incomplete parameters")
        values = [float(token) for token in tokens[index : index + count]]
        index += count
        relative = command.islower()
        previous = state.current

        if upper == "M":
            destination = absolute_point(values[0], values[1], relative)
            finish()
            state.current = destination
            state.start = destination
            points = [destination]
            command = "l" if relative else "L"
        elif upper == "L":
            state.current = absolute_point(values[0], values[1], relative)
            points.append(state.current)
        elif upper == "H":
            x = state.current[0] + values[0] if relative else values[0]
            state.current = (x, state.current[1])
            points.append(state.current)
        elif upper == "V":
            y = state.current[1] + values[0] if relative else values[0]
            state.current = (state.current[0], y)
            points.append(state.current)
        elif upper == "C":
            first = absolute_point(values[0], values[1], relative)
            second = absolute_point(values[2], values[3], relative)
            destination = absolute_point(values[4], values[5], relative)
            points.extend(cubic_points(previous, first, second, destination))
            state.current = destination
            state.last_cubic_control = second
            state.last_quadratic_control = None
            continue
        elif upper == "S":
            first = reflect(state.last_cubic_control, previous)
            second = absolute_point(values[0], values[1], relative)
            destination = absolute_point(values[2], values[3], relative)
            points.extend(cubic_points(previous, first, second, destination))
            state.current = destination
            state.last_cubic_control = second
            state.last_quadratic_control = None
            continue
        elif upper == "Q":
            control = absolute_point(values[0], values[1], relative)
            destination = absolute_point(values[2], values[3], relative)
            points.extend(quadratic_points(previous, control, destination))
            state.current = destination
            state.last_quadratic_control = control
            state.last_cubic_control = None
            continue
        elif upper == "T":
            control = reflect(state.last_quadratic_control, previous)
            destination = absolute_point(values[0], values[1], relative)
            points.extend(quadratic_points(previous, control, destination))
            state.current = destination
            state.last_quadratic_control = control
            state.last_cubic_control = None
            continue
        elif upper == "A":
            large_arc_value, sweep_value = values[3], values[4]
            if large_arc_value not in (0.0, 1.0) or sweep_value not in (0.0, 1.0):
                raise ValueError("SVG arc flags must be 0 or 1")
            destination = absolute_point(values[5], values[6], relative)
            points.extend(
                arc_points(
                    previous,
                    (values[0], values[1]),
                    values[2],
                    bool(large_arc_value),
                    bool(sweep_value),
                    destination,
                )
            )
            state.current = destination
            state.last_cubic_control = None
            state.last_quadratic_control = None
            continue

        state.last_cubic_control = None
        state.last_quadratic_control = None
    finish()
    return paths


def scaled_point(
    point: tuple[float, float], view_box: tuple[float, float, float, float],
    output_extent: int, gutter: int,
) -> tuple[float, float]:
    left, top, width, height = view_box
    if width != height:
        raise ValueError("Editor icon SVG viewBox must be square")
    scale = (output_extent - 2 * gutter) / width
    return (
        gutter + (point[0] - left) * scale,
        gutter + (point[1] - top) * scale,
    )


def draw_round_polyline(
    draw: ImageDraw.ImageDraw, points: list[tuple[float, float]], width: int,
    closed: bool = False,
) -> None:
    if len(points) < 2:
        return
    line_points = points + [points[0]] if closed else points
    draw.line(line_points, fill=255, width=width, joint="curve")
    radius = width * 0.5
    cap_points = points if closed else [points[0], points[-1]]
    for x, y in cap_points:
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=255)
    for x, y in points[1:-1]:
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=255)


def validated_svg(source: Path) -> tuple[ET.Element, tuple[float, float, float, float]]:
    root = ET.fromstring(source.read_text(encoding="utf-8"))
    if local_name(root.tag) != "svg":
        raise ValueError(f"{source}: root element must be svg")
    unknown_root = set(root.attrib) - ALLOWED_ROOT_ATTRIBUTES
    if unknown_root:
        raise ValueError(f"{source}: unsupported root attributes {sorted(unknown_root)}")
    if root.attrib.get("fill") != "none" or root.attrib.get("stroke") != "currentColor":
        raise ValueError(f"{source}: icon must be an unfilled currentColor stroke")
    if root.attrib.get("stroke-linecap") != "round" or root.attrib.get("stroke-linejoin") != "round":
        raise ValueError(f"{source}: icon must use round stroke caps and joins")
    view_box_values = root.attrib.get("viewBox", "").split()
    if len(view_box_values) != 4:
        raise ValueError(f"{source}: viewBox must contain four numbers")
    view_box = tuple(
        parse_number(value, f"{source}: viewBox") for value in view_box_values
    )
    for child in root:
        element = local_name(child.tag)
        if element not in ALLOWED_ELEMENTS:
            raise ValueError(f"{source}: unsupported SVG element {element}")
        unknown_attributes = set(child.attrib) - ALLOWED_ELEMENT_ATTRIBUTES
        if unknown_attributes:
            raise ValueError(
                f"{source}: unsupported {element} attributes {sorted(unknown_attributes)}"
            )
        if list(child):
            raise ValueError(f"{source}: nested SVG elements are not supported")
    return root, view_box  # type: ignore[return-value]


def point_attribute(element: ET.Element, x_name: str, y_name: str) -> tuple[float, float]:
    return (
        parse_number(element.attrib.get(x_name, "0"), f"{local_name(element.tag)} {x_name}"),
        parse_number(element.attrib.get(y_name, "0"), f"{local_name(element.tag)} {y_name}"),
    )


def render_svg(
    source: Path, raster_extent: int, gutter: int, supersample: int
) -> Image.Image:
    root, view_box = validated_svg(source)
    output_extent = raster_extent * supersample
    output_gutter = gutter * supersample
    scale = (output_extent - 2 * output_gutter) / view_box[2]
    stroke_width = max(
        1,
        round(parse_number(root.attrib.get("stroke-width", "2"), "stroke-width") * scale),
    )
    image = Image.new("L", (output_extent, output_extent), 0)
    draw = ImageDraw.Draw(image)

    def scaled(points: Iterable[tuple[float, float]]) -> list[tuple[float, float]]:
        return [
            scaled_point(point, view_box, output_extent, output_gutter)
            for point in points
        ]

    for element in root:
        kind = local_name(element.tag)
        if kind == "path":
            for points, closed in parse_path(element.attrib.get("d", "")):
                draw_round_polyline(draw, scaled(points), stroke_width, closed)
        elif kind == "line":
            draw_round_polyline(
                draw,
                scaled(
                    [
                        point_attribute(element, "x1", "y1"),
                        point_attribute(element, "x2", "y2"),
                    ]
                ),
                stroke_width,
            )
        elif kind == "circle":
            center = point_attribute(element, "cx", "cy")
            radius = parse_number(element.attrib.get("r", "0"), "circle r")
            left_top = scaled_point(
                (center[0] - radius, center[1] - radius), view_box,
                output_extent, output_gutter
            )
            right_bottom = scaled_point(
                (center[0] + radius, center[1] + radius), view_box,
                output_extent, output_gutter
            )
            draw.ellipse((*left_top, *right_bottom), outline=255, width=stroke_width)
        elif kind == "rect":
            left_top_source = point_attribute(element, "x", "y")
            width = parse_number(element.attrib.get("width", "0"), "rect width")
            height = parse_number(element.attrib.get("height", "0"), "rect height")
            right_bottom_source = (
                left_top_source[0] + width,
                left_top_source[1] + height,
            )
            left_top = scaled_point(
                left_top_source, view_box, output_extent, output_gutter
            )
            right_bottom = scaled_point(
                right_bottom_source, view_box, output_extent, output_gutter
            )
            radius = parse_number(element.attrib.get("rx", "0"), "rect rx") * scale
            draw.rounded_rectangle(
                (*left_top, *right_bottom),
                radius=radius,
                outline=255,
                width=stroke_width,
            )
        elif kind in ("polyline", "polygon"):
            values = [
                parse_number(value, f"{kind} points")
                for value in re.findall(NUMBER_PATTERN, element.attrib.get("points", ""))
            ]
            if len(values) < 4 or len(values) % 2 != 0:
                raise ValueError(f"{source}: {kind} points must contain coordinate pairs")
            points = list(zip(values[0::2], values[1::2], strict=True))
            draw_round_polyline(draw, scaled(points), stroke_width, kind == "polygon")

    result = image.resize(
        (raster_extent, raster_extent), resample=Image.Resampling.LANCZOS
    )
    ImageDraw.Draw(result).rectangle(
        (0, 0, raster_extent - 1, raster_extent - 1), outline=0, width=1
    )
    if result.getbbox() is None:
        raise ValueError(f"{source}: rasterized icon is empty")
    return result


def load_manifest() -> tuple[int, int, int, int, list[IconSpec]]:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    logical_extent = int(manifest["logical_extent"])
    raster_extent = int(manifest["raster_extent"])
    gutter = int(manifest["gutter"])
    supersample = int(manifest["supersample"])
    if logical_extent <= 0 or raster_extent <= 0 or supersample <= 0 or gutter < 1:
        raise ValueError("Editor icon extents/supersample must be positive and gutter non-zero")
    if raster_extent % logical_extent != 0:
        raise ValueError("Editor raster extent must be an integer logical scale")
    if gutter * 2 >= raster_extent:
        raise ValueError("Editor icon gutter leaves no drawable pixels")
    icon_root = MANIFEST_PATH.parent
    icons: list[IconSpec] = []
    names: set[str] = set()
    files: set[Path] = set()
    for entry in manifest["icons"]:
        name = entry["name"]
        if ENUM_NAME_PATTERN.fullmatch(name) is None or name in names:
            raise ValueError(f"invalid or duplicate Editor icon name: {name!r}")
        source = (icon_root / entry["file"]).resolve()
        if icon_root.resolve() not in source.parents or source.suffix != ".svg":
            raise ValueError(f"Editor icon source escapes the SVG resource root: {source}")
        if source in files or not source.is_file():
            raise ValueError(f"missing or duplicate Editor icon source: {source}")
        names.add(name)
        files.add(source)
        icons.append(IconSpec(name=name, source=source))
    if not icons:
        raise ValueError("Editor icon manifest must contain at least one icon")
    if len(icons) > 255 or raster_extent * len(icons) > 65535:
        raise ValueError("Editor icon atlas exceeds its C++ enum or Texture2D extent")
    return logical_extent, raster_extent, gutter, supersample, icons


def render_header(
    logical_extent: int, raster_extent: int, gutter: int,
    icons: list[IconSpec],
) -> str:
    raster_scale = raster_extent // logical_extent
    enum_lines = "\n".join(f"    {icon.name}," for icon in icons)
    rect_lines = "\n".join(
        "        EditorIconAtlasRect{\n"
        f"            .x = {index}U * EditorIconRasterExtent,\n"
        "            .y = 0U,\n"
        "            .width = EditorIconRasterExtent,\n"
        "            .height = EditorIconRasterExtent,\n"
        "        },"
        for index, _ in enumerate(icons)
    )
    return f"""// Generated by tools/editor_icons/cook_editor_icons.py. Do not edit.
#pragma once

#include <tina/core/base/Types.hpp>

#include <array>

namespace Tina::EditorApp::WorkspaceInternal {{

inline constexpr Core::u32 EditorIconLogicalExtent = {logical_extent}U;
inline constexpr Core::u32 EditorIconRasterScale = {raster_scale}U;
inline constexpr Core::u32 EditorIconRasterExtent = {raster_extent}U;
inline constexpr Core::u32 EditorIconRasterGutter = {gutter}U;

enum class EditorIcon : Core::u8 {{
{enum_lines}
    Count,
}};

inline constexpr Core::u32 EditorIconCount =
    static_cast<Core::u32>(EditorIcon::Count);
inline constexpr Core::u32 EditorIconAtlasWidth =
    EditorIconRasterExtent * EditorIconCount;
inline constexpr Core::u32 EditorIconAtlasHeight = EditorIconRasterExtent;

struct EditorIconAtlasRect final {{
    Core::u32 x = 0U;
    Core::u32 y = 0U;
    Core::u32 width = 0U;
    Core::u32 height = 0U;
}};

inline constexpr std::array<EditorIconAtlasRect, EditorIconCount>
    EditorIconAtlasRects{{{{
{rect_lines}
    }}}};

}} // namespace Tina::EditorApp::WorkspaceInternal
"""


def wrapped_values(values: Iterable[int], indentation: str = "    ") -> str:
    lines: list[str] = []
    current = indentation
    for value in values:
        token = f"{value},"
        if len(current) + len(token) + 1 > 112:
            lines.append(current.rstrip())
            current = indentation
        current += token + " "
    if current.strip():
        lines.append(current.rstrip())
    return "\n".join(lines)


def render_alpha(
    raster_extent: int, gutter: int, supersample: int, icons: list[IconSpec]
) -> str:
    atlas = Image.new("L", (raster_extent * len(icons), raster_extent), 0)
    for index, icon in enumerate(icons):
        atlas.paste(
            render_svg(icon.source, raster_extent, gutter, supersample),
            (index * raster_extent, 0),
        )
    values = wrapped_values(atlas.tobytes())
    return f"""// Generated by tools/editor_icons/cook_editor_icons.py. Do not edit.
inline constexpr std::array<Core::u8,
                            EditorIconAtlasWidth * EditorIconAtlasHeight>
    EditorIconAtlasAlpha{{{{
{values}
    }}}};
"""


def update_file(path: Path, content: str, check: bool) -> bool:
    current = path.read_text(encoding="utf-8") if path.exists() else None
    if current == content:
        return False
    if check:
        print(f"stale generated Editor icon file: {path.relative_to(REPOSITORY_ROOT)}")
        return True
    path.write_text(content, encoding="utf-8", newline="\n")
    print(f"wrote {path.relative_to(REPOSITORY_ROOT)}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify committed generated files without changing them",
    )
    arguments = parser.parse_args()
    logical_extent, raster_extent, gutter, supersample, icons = load_manifest()
    header = render_header(logical_extent, raster_extent, gutter, icons)
    alpha = render_alpha(raster_extent, gutter, supersample, icons)
    changed = update_file(GENERATED_HEADER_PATH, header, arguments.check)
    changed |= update_file(GENERATED_ALPHA_PATH, alpha, arguments.check)
    return 1 if arguments.check and changed else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ET.ParseError, KeyError, TypeError, ValueError) as error:
        print(f"Editor icon cook failed: {error}", file=sys.stderr)
        sys.exit(2)
