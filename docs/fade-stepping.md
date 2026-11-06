# Fade investigation and current QMK heatmap: revision 15

Revision 15 replaces the earlier red/green color and decay experiments below
with QMK Typing Heatmap's default cooling and HSV mapping, including cyan/blue.
The low-green cutoff and special tail envelope are removed. Current behavior,
source attribution, RGB compensation and tests are documented in
[qmk-heatmap.md](qmk-heatmap.md). The following sections preserve the history
and evidence for revisions 9–14; their timings are not revision 15's behavior.

The user reported roughly one-second dim-color batches and a shallow red tail
on a build preceding r9. Revision 9 changed notification handling only; it did
not change the animation. The following reproduction and fix concern fading.

## What the old tests missed

The previous tests counted RGB values in the received palette and checked packet
order. The side Layer renderer applies a gamma table and channel gains before
writing its LED output buffer. Many distinct palette colors collapse to the same
output. Checking only palette bytes substantially overstated visible smoothness.

Running the exact released instructions on an isolated tap gives this sequence
for the pressed key in revisions 5–9, before global brightness scaling:

| Time | Post-gamma RGBA output |
| --- | --- |
| 0 ms | (0, 3, 0, 0) |
| 440 ms | (0, 1, 0, 0) |
| 1400 ms | (0, 0, 0, 0) |

The dim green holds for **960 ms**. For a saturated key there are 124 distinct
output colors, with a final 980 ms green hold before black. Sending more frequent
packets cannot fix quantization inside this conversion.

## Source-only correction

The Neuron maps desired linear red/green values through the inverse of the exact
released transfer. `src/SplashColor.h` contains two small, generated lookup tables.
`tools/generate-splash-color.py` accepts only the SHA-verified official 1.5.0 side
image, reads the gamma LUT at `0x100267a4` and the R/G float32 gains at
`0x10012f68` / `0x10012f60`, then selects the closest realizable output. Black maps
to zero. The build verifies the generated header; it never modifies the side.

At ordinary energy the existing decay law is unchanged. Revision 10 used a
linear tail, which reduced the final holds to alternating 160/320 ms intervals.
Revision 11 introduced equal 250 ms holds. Revision 12 reduces them to 50 ms, skipping
raw RGB values that produce the same output. This begins below about 16/255.
Each key has its own hold phase; a new tap adds heat and restarts the affected
hold. Tap energy, splash rings, frame pacing and brightness configuration stay
the same.
This makes low-energy taps brighter: their brightness is no longer unintentionally
crushed by another gamma conversion.

## Brightness reversals from palette regrouping

The user subsequently reported brief higher/lower/higher brightness during a
fade. This was reproduced without any new keypresses: per-frame nearest-energy
regrouping changed a declining key's represented color upward. Twenty seeded
histories produced 113 post-gamma brightening reversals in revision 11.

Revision 12 builds groups after each new press, then preserves their membership
while energy decays. Every frame uses each group's mean current energy. Finished
members stay in its denominator, so their removal cannot brighten the others.
The same rule is used on the first frame, avoiding a jump from a merge center
to a different mean on the following frame. New input rebuilds only its own half.

`tests/splash/jitter.cpp` checks 230,340 cold-fade key samples across those
histories. The new implementation has no brightening reversals; compiling the
original header reproduces them. The released renderer replay also verifies
nonincreasing red and a single rise/fall of green across complete no-input fades.
A green increase during the initial red-to-green cooling phase is intentional;
a later rebound after green begins falling is rejected.

## Revision 13: remove the shade holds

The user still saw green linger and then drop to black. The low-end code did
explicitly hold each shade: it stopped updating the energy until a 50 ms timer
expired, then selected the next realizable color. Those holds are now removed.

Below about 16/255, each key records the current energy and evolves it every
10 ms using `start_energy * remaining_time^2 / duration^2`, with a 1000 ms
nominal duration. This preserves Q8 fractional brightness between output-level
boundaries and reduces the brightness loss as the ending progresses. A new
press adds heat and restarts that key's tail from its new level. Higher-energy
exponential decay and fixed palette membership during no-input fades remain.

The native test starts a known 12/255 neighbor directly in the tail, checks every
10 ms against the expected curve, verifies decreasing energy between RGB
boundaries, and checks the decreasing loss. Packet tests then verify the last
five visible green levels take 400–750 ms in total, with the last lit level
lasting 150–300 ms. That is a measured quantized output bound, not an explicit
hold schedule. The measured full-brightness renderer trace spends 540 ms across
those levels, including 220 ms at the minimum lit level. Thus a visible final
plateau and jump remain possible; this does not add lower physical PWM values.
The native and released-renderer tests still require monotone
cold fades, no brightness reversals and final black.

## Revision 14: shorten the ending and skip faint green

The user rejected the long plateaus and requested removal of the dark-green
ending. The tail now uses `start_energy * remaining_time / duration`, with a
500 ms duration and 10 ms updates. It no longer slows down near zero.
Rounded energy 0..8 is encoded as black, which removes the released renderer's
faint G=1,3,4,6,7 outputs. The cutoff is applied after group averaging too, so
busy palette groups cannot reintroduce those values. Unrounded heat remains
available for additive reheating; the high-energy decay is unchanged.

This deliberately increases the last jump: G=9 goes directly to black. Very
faint outer rings also remain dark until enough heat accumulates. It removes
the animation phase the user associates with red; it does not establish the
physical cause or guarantee that emitted red is gone.

## Verification and limits

`tests/splash/color_transfer.py` executes the released ARM Layer renderer and
checks every RGBW byte against the independently extracted gamma/gain oracle.
The single-tap fade has 27 distinct outputs including black; the saturated
model fade has 164. The renderer's final G=9 lasts 40 ms, then black, with none
of the five removed faint-green outputs on any key, including overlapping
splashes. A low green channel mixed with strong red is still allowed.
The plugin's paced packet trace measures 30 ms at the final G=9.
The model checks the linear envelope, additive reheating and all 256 rounded
input energies. Monotone fades, no brightness reversals, routing, input and
storage regressions remain required. The intentional G9-to-black transition
is the sole exception to the prior small per-frame change bound.

These results describe the output buffer before global brightness scaling.
Lower global brightness reduces the physical PWM levels available, and real
LEDs cannot show infinitely small brightness steps. This change has not yet
been observed on the physical keyboard.

The reported red tail was not reproduced by the old isolated fade: it went green
to black. The corrected isolated, saturated and overlapping renderer tests also
show no nearly-black red tail, but this does not establish the cause of the
physical report. Hardware confirmation is still needed; the gamma result alone
must not be presented as a proven explanation for the red tail.

The user also sees a reddish tint while Bazecor's pure white is steadily lit.
That observation makes the shared color-conversion/LED path a candidate for
investigation, but does not identify hardware, channel calibration or another
specific cause. In r13, the final green levels (7, 6, 4, 3, 1) supplied R=0, B=0 and W=0.
Those faint levels are now skipped; all channels reach zero at black in the renderer replay. No speculative global color-balance
change is included in r14, and the reported physical tint remains unresolved.

Further tests now extend through the released PWM packer and output queue.
A separately selected anti-ghost experiment changes one documented driver
setting, while the ordinary r14 package retains the existing map-only side image. See
[the tail/driver investigation](tail-driver-investigation.md) for measured
limits, exact patch details and the still-unconfirmed physical cause.
