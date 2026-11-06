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
red, green, blue = color_tool.transfer_tables()
side = ScheduledReceiver(image, 33)
side.update(0, False, [1] * 33 + [0] * 3)
for value in range(256):
    side.update_palette(1, bytes([value, value, value, 0]))
    side.command(value * 20)
    side.poll(value * 20 + 10)
    assert side.flushes[-1][1][:4] == bytes([red[value], green[value], blue[value], 0])
print('Actual RP2040 Layer gamma/channel gains: all 256 RGB inputs match the compensation oracle')

trace = Path(sys.argv[1]).read_bytes()
stride = 100 + 144
assert len(trace) == stride * 851 * 3
for scenario in range(3):
    side = ScheduledReceiver(image, 33)
    colors = set()
    runs = []
    previous = None
    channel_falling = [[False]*3 for _ in range(33)]
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
                assert max(actual[at:at+3]) <= max(previous[at:at+3]), ('peak rebound', scenario, n, led)
                for channel in range(3):
                    if actual[at+channel] < previous[at+channel]:
                        channel_falling[led][channel] = True
                    if channel_falling[led][channel]:
                        assert actual[at+channel] <= previous[at+channel], ('channel rebound', scenario, n, led, channel)
        previous = actual
        rgb = tuple(actual[8*4:8*4+4])
        colors.add(rgb)
        if not runs or runs[-1][1] != rgb:
            runs.append((n*20, rgb))
        # QMK cold tail is pure blue; it must not acquire red/green or white.
        for led in range(33):
            r, g, b, w = actual[led*4:led*4+4]
            assert w == 0
            if max(r, g, b) <= 7:
                assert r == 0 and g == 0, (scenario, n, led, r, g, b)
        assert not any(actual[33*4:])
    assert not any(actual)
    if scenario < 2:
        assert len(colors) >= (30 if scenario == 0 else 200)
        last_time,last_rgb=runs[-2]
        last_hold=runs[-1][0]-last_time
        assert last_rgb[0]==0 and last_rgb[1]==0 and 0<last_rgb[2]<=3 and last_rgb[3]==0
        assert 20<=last_hold<=60, (last_rgb,last_hold)
        off=runs[-1][0]
        assert off==(800 if scenario==0 else 6380), off
        if scenario==0: assert all(rgb[0]==0 and rgb[1]==0 for _,rgb in runs)
        else:
            assert any(r==0 and g>120 and b>120 for _,(r,g,b,w) in runs)
            assert any((r,g,b)==(0,138,0) for _,(r,g,b,w) in runs)
            assert any((r,g,b)==(0,0,138) for _,(r,g,b,w) in runs)
        print(f'Actual RP2040 QMK {"single-tap" if scenario == 0 else "saturated"} fade: {len(colors)} output colors; black at {off} ms; pure-blue ending; final level {last_hold} ms')
    else:
        print('Actual RP2040 QMK overlapping-splash fade: every key matches RGB oracle; no peak/channel reversals after peaks; cold pure-blue tail; final black')
