# QMK-style heatmap — revision 15

The requested reference is QMK's Typing Heatmap, demonstrated on a Keyball in
https://www.youtube.com/shorts/hIqxDwHeHTI and described by its author at
https://note.com/co_ke/n/nf89d0de0d718.

The implementation uses the default QMK +32 heat per press and -1 heat every
25 ms. Hue is `170 - max(heat - 85, 0)`; HSV value is `min(heat, 85) * 3`,
scaled to the effect's attainable brightness ceiling. Cooling runs red, yellow,
green, cyan, blue, then a pure-blue fade to black. A single press starts blue;
repeated presses build up hotter colors. There are no manual shade timers,
exponential slow-downs, dim-green tail, or artificial low-energy cutoff.

The exact referenced QMK commit is
`f5db962110615fd263c3ebf9f0402e7c2b08fbe4`:

- [Heatmap source](https://github.com/qmk/qmk_firmware/blob/f5db962110615fd263c3ebf9f0402e7c2b08fbe4/quantum/rgb_matrix/animations/typing_heatmap_anim.h)
- [HSV conversion](https://github.com/qmk/qmk_firmware/blob/f5db962110615fd263c3ebf9f0402e7c2b08fbe4/quantum/color.c)

Copies and SHA256 metadata live in `tests/splash/qmk_reference`. The unmodified
upstream `color.c` is compiled into the native tests, using the no-CIE conversion:
the Raise 2's separate gamma stage is explicitly compensated. Production's
linear RGB results must match that upstream conversion for all 256 heat levels.
The adapted conversion credits Jason Williams and QMK; GPL-2.0-or-later code is
used under this project's GPL-3.0 license.

## Raise 2 adaptation

The original half-strength neighbor rings remain: 32 at the center, 16 in the
first ring, then 8, 4, 2, 1 and fractional heat. These use the existing approximate
key grid, include diagonal neighbors and do not cross the split. QMK itself
uses a configurable physical-distance spread and capped neighbor contribution;
this adaptation retains the user's earlier requested ring falloff.

Fractional heat is retained for additive presses, and rounds to the nearest
integer when selecting the QMK color. All elapsed 25 ms cooling steps are applied
after stalls. From a newly reset clock a single center reaches zero in 800 ms
and a saturated center in 6375 ms. The periodic cooling phase and paced output
can shift the first/last visible packet slightly.

The stock side Layer renderer applies one gamma table plus gains R=1.49,
G=1.54 and B=0.545. Its blue maximum is 138, so all effect channels use the same
attainable 138 ceiling. This maintains the intended RGB proportions and a
constant *peak PWM channel* during hot hue changes without clipping blue.
It does not claim constant perceived brightness or calibrated LED chromaticity.
Bazecor's global brightness remains unchanged and is still applied afterwards.
Inverse gamma/gain tables now cover blue as well as red and green. The actual
released renderer verifies all 256 raw input values on every RGB channel.

Palette groups remain stable between presses and include zero-energy members
in their mean to prevent the previously reproduced brightness rebounds.
Per-layer Bazecor colors stay suppressed while the overlay owns the LEDs.
Transport, notifications, input decoding, configuration storage and default
keymap are unchanged by revision 15. The existing map-allocation-patched side
image is byte-identical to ordinary r2–r14. No new blob patch is included.

## Verification

- All 256 heat colors match the pinned QMK HSV reference.
- Fixed cooling, additive reheating, long-gap catch-up, clock rollover,
  neighbor ring strength and both-half matrix positions are checked.
- Twenty seeded overlapping histories check 230340 key samples for peak
  brightness rebounds. Intentional green/blue channel rises during cooling
  are allowed; reversals after a channel starts falling are rejected by replay.
- Released ARM renderer replay: 33 single-tap output colors, black at 800 ms;
  251 saturated output colors, black on the 6380 ms frame. Both finish through
  pure blue, with the final lit level lasting 20 ms at the test cadence.
- Actual plugin packets: 33 single-tap colors and a 787 ms visible span; 251
  saturated colors. Lifecycle, exclusive LED ownership, retry and restore,
  normal routing and the notification regressions remain required.
- Build verification retains exact stock key-decoder/storage objects and the
  320-byte default keymap, with only the previously verified disconnect guards
  allowed in KeyboardManager.

These checks do not measure emitted light or establish that the earlier physical
red tint is gone. The new Neuron image has not been observed on the keyboard.
Select `build/install/splash-heatmap-v1.5.0-r15` in Bazecor's custom firmware
picker after backing up the current input profile. Packaging never flashes.
