# Pre-QMK retry: shade delay decreases from 50 ms to zero — r16

This candidate returns to r14's red/green color mapping, +48 tap boost,
half-strength neighbor rings, original R/G gamma compensation and high-energy
exponential cooling. The faint-green cutoff requested before QMK is retained:
rounded heat 0..8 encodes black. Blue and white are always zero. QMK's hue
mapping, +32 heating, fixed cooling and 138 RGB ceiling are not active.

The requested change is the *time between dimming steps*, not the physical
keypress-to-light response. When a key enters the low-energy tail, it counts
its remaining distinct encoded colors. The delay is

    50 * (remaining_shades - 1) / (initial_shades - 1) milliseconds

For the full six-shade tail this gives 50, 40, 30, 20, 10 and 0 ms. A shorter
tail also starts at 50 ms and decreases linearly: three shades give 50, 25, 0.
The model runs on 10 ms ticks, so a 25 ms wait rounds up to 30 ms. A terminal
zero-delay shade is consumed in the same model tick; it does not add a wait.
New heat resets the affected key's tail timing. The high-energy decay above
about 16/255 is unchanged.

This does not make the full animation a 150 ms fade. The full low-energy tail
has 150 ms of nominal waits; the earlier high-energy cooldown remains long.
The first tail color may already have been visible above the tail threshold,
so its total visible duration can exceed the nominal 50 ms tail wait.

The model checks the full delay sequence and additive reheating. Released ARM
renderer replay measures a single center off at 2820 ms and a saturated center
off at 7040 ms at the 20 ms test cadence. The last visible center RGB is
(0,10,0), lasting 20 ms in that replay. Very short model stages may not appear
as separate physical frames. The paced plugin is tested separately, and input,
storage, route/disconnect and transport regressions remain required.

## Scope and limitations

The palette/map/MODE transport, its 50 fps cap, and the released side's forced
10 ms redraw delay are unchanged. The shared palette still has 15 nonblack
colors per half. These limits remain relevant to the reported stepping and lag;
this is the requested pre-QMK timing experiment, not a claimed fix for all three
r15 complaints. The physical red tint is still unresolved.

The side image is byte-identical to the ordinary r2–r15 map-allocation-patched
image. No new binary patch, bootloader edit, recovery change, periodic refresh
or runtime diagnostics are introduced. Nothing is automatically flashed.

Installable candidate: `build/install/splash-heatmap-v1.5.0-r16`.
Keep a Bazecor profile backup and verify typing after any update before enabling
Splash. Earlier stock and custom installation folders remain intact.
