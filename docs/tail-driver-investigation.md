# End-of-fade investigation and separate anti-ghost experiment

Revision 15 uses the QMK heatmap mapping and a pure-blue cold fade instead of
the red/green animation investigated below. It keeps the ordinary side binary
unchanged. The physical red tint remains unconfirmed; these earlier observations
and the separately packaged r12 anti-ghost experiment are retained as evidence.
See [the current QMK implementation](qmk-heatmap.md).

The user confirmed r12 improves the earlier fade but every colored key ends with
faint red before black, and the remaining green turns off sharply. This is not a
report about a few fixed keys. A live read confirmed r12, Splash enabled and
backlight brightness 255. No LED or profile settings were changed by those reads.

## Green cutoff

The final displayed levels 7, 6, 4, 3, 1 each last 50 ms, so this part of r12's
fade is only 250 ms long. The last static output is PWM 1, then zero. The existing
color-map protocol and driver output cannot express a static intermediate level
between those two values. Increasing the packet rate does not add that level.
Extending those holds can lengthen the ending but cannot make that last step
continuous. Slow host-side temporal dithering would introduce flicker and is not
included as a claimed smoothness fix.

The driver hardware has separate PWM and current-scaling controls, but the
current Neuron palette/map path exposes only RGBW bytes and brightness that is
multiplied/truncated before output. A proper finer-range ending would require
additional side-driver control, rather than another palette timing adjustment.
The experiment below does not claim to solve the green cutoff.

Revision 13 removes the explicit shade timers and uses a continuous Q8
quadratic tail instead. At full brightness, the renderer still produces the
same five dim levels: 540 ms in total, including 220 ms at PWM 1. It changes
the fade envelope, not the minimum physical step; a perfectly smooth physical
ending is not established by that change.

Revision 14 subsequently shortens the tail to a 500 ms linear envelope and
skips the faint G=1,3,4,6,7 outputs entirely. Its last lit output is G=9 for
40 ms in the renderer replay, then black. This is an earlier animation cutoff,
not new static PWM precision or proof that the physical red tint is fixed.
The ordinary side image and the separate r12 anti-ghost experiment stay unchanged.

## Test beyond the renderer buffer

`tests/splash/side_driver.py` extends the previous emulation through the released
RGBW mapping, PWM packing and output queue boundary. It uses an explicit identity
map fixture for 33 logical keys and 44 driver slots; physical PCB wiring, SPI
hardware and analog LED behavior are not simulated.

The released code queues R=0/G=1/B=0/W=0 for the final green stage. When all color
bytes reach black, it drives the LED-driver control pin low and queues no zero-PWM
frame; the prior PWM data remains in the software driver buffer. The observed
control-pin edge suggests a shutdown-path or matrix-ghosting investigation, but
it does not prove the physical cause of the user's red phase. The test cannot
simulate charge decay, pin voltages or emitted light.

## Optional anti-ghost test — DIFFERENT SIDE IMAGE

The released driver initialization writes `52 02 B3` over SPI. The separately
selected experiment writes `52 02 B1`. This changes the IS31FL3743B function-page
02h CS pull-up selection from 2kΩ to 0.5kΩ during CS off time. SW pull-down
selection and phase are unchanged. PWM and global LED-current settings are
unchanged. The manufacturer documents these selections and their purpose in
Table 8 and the de-ghost section of its datasheet:

https://www.lumissil.com/assets/pdf/core/IS31FL3743B_DS.pdf

The extra binary change is one instruction byte inside a declared two-byte
window at execution address `0x1000a272`: `a03b` becomes `a23b`, changing an
immediate subtraction. Both image CRC fields are recalculated. The ordinary
map-allocation fixes remain. All other bytes, addresses, size, version and reset
vectors remain unchanged. This is a bounded experiment, not a proven fix or a
claim of hardware safety, power-consumption or recovery validation.

Emulation executes the actual initialization state and captures the resulting
SPI bytes for ordinary and experimental images. Both output paths retain the
same final-green/black behavior. The experiment also passes 10,000 map updates,
mode/palette tests, CRC/hash/change-boundary checks and unexpected-input rejection.

The experimental side SHA256 is:

    f06a49cbb88f257665de466b7bd53bb86b98df0264955c8e0d9c159fa192562b

## Build and compare

From the main firmware checkout, with the existing raise2-toolchain venv:

```bash
~/.local/opt/raise2-toolchain/venv/bin/python tests/splash/side_driver.py > build/verification/side-driver-tests.log
python build/package-deghost-experiment.py
```

Select the SEPARATE test folder only if choosing to test the changed side image:

    build/install/splash-heatmap-v1.5.0-r12-deghost-test

It reuses the exact verified r12 Neuron HEX and changes only the side image. Both
halves need the experimental image for a fair comparison. The version response
still says r12; the manifest and side application CRC distinguish this test.
Keep the normal sibling `splash-heatmap-v1.5.0-r12` folder to undo the experiment.
It contains the prior map-only side image; selecting it permits Bazecor to restore
that image through its regular updater. Physical rollback is not newly claimed
tested. The official 1.5.0 rollback files also remain available locally.

Back up the current Bazecor configuration before any update. Verify that Bazecor
restores it afterwards, and test typing before enabling Splash. The helper's
`--verify-backup` option checks the input profile before enabling it. No packaging
script opens a keyboard or flashes firmware.

The effect of the resistor change on actual red ghosting is unconfirmed. If the
physical red phase remains, that is evidence against this particular setting as
a sufficient solution; it does not justify reporting the tint as fixed.
