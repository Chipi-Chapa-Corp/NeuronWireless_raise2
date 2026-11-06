#!/usr/bin/env python3
"""Validate fades AFTER the actual released Layer renderer's gamma/gain stage."""
from pathlib import Path
import importlib.util
import sys
from side_binary import root, patcher
from side_scheduler import ScheduledReceiver

spec = importlib.util.spec_from_file_location('color_tool', root / 'tools/generate-splash-color.py')
color_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(color_tool)
image = patcher.patch((root / 'build/vendor/keyscanner-v1.5.0.bin').read_bytes())
red, green = color_tool.transfer_tables()
side = ScheduledReceiver(image, 33)
side.update(0, False, [1] * 33 + [0] * 3)
for value in range(256):
    side.update_palette(1, bytes([value, value, 0, 0]))
    side.command(value * 20)
    side.poll(value * 20 + 10)
    assert side.flushes[-1][1][:4] == bytes([red[value], green[value], 0, 0])
print('Actual RP2040 Layer gamma/channel gains: all 256 red/green inputs match the compensation oracle')

trace = Path(sys.argv[1]).read_bytes()
stride = 100 + 144
assert len(trace) == stride * 851 * 3
for scenario in range(3):
    side = ScheduledReceiver(image, 33)
    colors = set()
    runs = []
    previous = None
    green_falling = [False] * 33
    for n in range(851):
        offset = (scenario * 851 + n) * stride
        frame, expected = trace[offset:offset+100], trace[offset+100:offset+stride]
        side.update_palette(0, frame[36:60])
        side.update_palette(6, frame[60:84])
        side.update_palette(12, frame[84:100])
        side.update(0, False, list(frame[:36]))
        side.command(n * 20)
        side.poll(n * 20 + 10)
        actual = side.flushes[-1][1]
        assert actual == expected, (scenario, n, actual, expected)
        if previous is not None:
            for led in range(33):
                at = led * 4
                assert actual[at] <= previous[at], ('red rebound', scenario, n, led)
                if actual[at+1] < previous[at+1]:
                    green_falling[led] = True
                if green_falling[led]:
                    assert actual[at+1] <= previous[at+1], ('green rebound', scenario, n, led)
        previous = actual
        rgb = tuple(actual[8*4:8*4+4])
        colors.add(rgb)
        if not runs or runs[-1][1] != rgb:
            runs.append((n*20, rgb))
        # After a fixed history stops, a nearly black green must not turn red.
        for led in range(33):
            r, g, b, w = actual[led*4:led*4+4]
            if max(r, g) <= 7:
                assert r == 0 and g == 0, (scenario, n, led, r, g)
        assert not any(actual[33*4:])
    assert not any(actual)
    if scenario < 2:
        assert len(colors) >= (23 if scenario == 0 else 155)
        assert all(rgb[0]>0 or rgb[1]==0 or rgb[1]>7 for _,rgb in runs), runs
        last_time,last_rgb=runs[-2]
        last_hold=runs[-1][0]-last_time
        assert last_rgb[0]<=1 and 10<=last_rgb[1]<=12 and last_rgb[2:]==(0,0) and 20<=last_hold<=60, (last_rgb,last_hold)
        print(f'Actual RP2040 accelerating-tail {"single-tap" if scenario == 0 else "saturated"} fade: {len(colors)} output colors; last RGB {last_rgb[:3]} for {last_hold} ms; no faint green; final black')
    else:
        print('Actual RP2040 overlapping-splash fade: every key matches post-gamma RGB, no brightness reversals or red tail, final black')
