#!/usr/bin/env python3
"""Source-grounded JUCE UI geometry contract; not a screenshot/render test."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
header = (root / "Source/ui/HeaderBar.cpp").read_text()
column = (root / "Source/ui/StemColumn.cpp").read_text()
editor = (root / "Source/PluginEditor.cpp").read_text()
processor = (root / "Source/PluginProcessor.cpp").read_text()

base = (1047.0, 597.0)
header_h = 107.0
column_size = (310.0, 475.0)
centers = (200.0, 518.0, 836.0)

def bounds(source: str, component: str):
    match = re.search(r"\bplace\(\s*" + re.escape(component) +
                      r"\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)\s*,\s*([0-9.]+)\s*\)", source)
    assert match, f"Missing explicit layout for {component}"
    return tuple(map(float, match.groups()))

def inside(rect, box):
    x, y, w, h = rect
    bx, by, bw, bh = box
    return x >= bx and y >= by and w > 0 and h > 0 and x+w <= bx+bw and y+h <= by+bh

def intersects(a, b):
    return a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]

controls = {name: bounds(header, name) for name in ("mode", "engine", "bypass", "previous", "next", "menu")}
for name, rect in controls.items():
    assert inside(rect, (0, 0, base[0], header_h)), name
# All header interactions must be independently targetable at 75% size.
for a in controls:
    for b in controls:
        if a < b:
            assert not intersects(controls[a], controls[b]), (a, b)
# Fixed sample-over indicator must not overlap an interactive header target.
over = (920.0, 48.0, 47.0, 20.0)
for name, rect in controls.items():
    assert not intersects(over, rect), f"OVER overlaps {name}"

panel = (7.5, 1.0, 295.0, 469.0)
for name in ("gain", "solo", "mute", "meter"):
    rect = bounds(column, name)
    assert inside(rect, panel), (name, rect)
for center in centers:
    assert inside((center-column_size[0]/2, 109, *column_size), (0, 0, *base)), center

# Track the real processor and editor contract, not a second invented layout.
assert "processor.setUiScale(juce::roundToInt(100.0f * getWidth() / Theme::designW))" in editor
assert "constrainer.setFixedAspectRatio(Theme::designW / Theme::designH)" in editor
assert "constrainer.setSizeLimits(785, 448, 1571, 896)" in editor
assert "NormalisableRange<float>(-40.0f, 40.0f, 0.1f), 0.0f" in processor
for size in ((785, 448), (1047, 597), (1571, 896)):
    sx, sy = size[0] / base[0], size[1] / base[1]
    for name, (x,y,w,h) in controls.items():
        target = (round(x*sx), round(y*sy), round(w*sx), round(h*sy))
        assert inside(target, (0, 0, size[0], round(header_h*sy))), (size, name)
print("Static JUCE geometry/default/resize contract passed at 75%, 100%, 150% (not a render test)")
