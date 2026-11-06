# Revision 15 rejected on hardware: color steps, latency and red tint

The user installed r15 and reported substantially worse smoothness, a noticeable
keypress-to-light delay, and more subtle red. This supersedes the earlier
software-only acceptance notes. r15 is not a recommended installation.
The keyboard was only queried for status in this investigation; no mode,
brightness, input settings, firmware or side binary was changed.

## Shared palette and color grouping

The available layer protocol stores four-bit indices into 16 shared RGBW
palette entries. Splash reserves black, leaving at most 15 nonblack colors
for all 33/36 keys on one half at a time. The Neuron implementation groups
nearby heat values to fit those slots and rebuilds groups after new input.
It does not have QMK's independent full RGB value for every LED.

A synthetic busy-typing audit used 20 seeded histories of 150 presses spaced
83 ms apart and compared every frame's grouped output with the ungrouped
ideal colors, after the released gamma/gain lookup. 2326 of 3000 frames lost
distinct key colors; the largest deviation of one RGB channel was 29/255.
This demonstrates color quantization during typing, not a proof that it alone
explains the severity of the physical report. Stable groups prevent the prior
no-input brightness rebound, but cannot prevent all color reassignment jumps
when presses trigger regrouping.

The earlier 251-color figure counted one key over a whole saturated cooldown.
It did not mean 251 simultaneous colors or establish perceptual smoothness on
the real keyboard. That metric was insufficient as an acceptance criterion.

## Press-to-light latency

`observe_keys()` changes heat, but does not directly change the half's LEDs.
`stream_frame()` waits for the frame schedule and queues palette chunks, a map
if necessary, and MODE_LED. The released Layer deserializer forces a 10 ms
redraw delay regardless of the serialized delay. Palette writes alone do not
redraw, and an already pending frame can predate a newly received press.

A native timing probe swept 20 repeat-press phases with one render loop per
millisecond and no transport backpressure. It measured 3–19 ms from the press
to the changed MODE packet. Add the released 10 ms side wait and *unmeasured*
physical transport and polling latency. This is a synthetic 13–29 ms minimum
path under the probe's assumptions, not an on-keyboard latency measurement and
not a complete explanation of a possibly larger observed delay.

Removing the serialized delay does not bypass the side's forced delay. Sending
unbounded bursts or blindly reducing the frame interval can overwrite a palette
before the side draws or reset its redraw timer repeatedly. Those are not
established fixes.

## Red: trace extended beyond the renderer

The red report is unresolved. The existing test did not measure emitted light.
An additional replay ran the released Layer renderer, brightness/output logic,
RGBW-to-driver mapping and final PWM packet packing, with an explicit identity
mapping fixture and SPI/GPIO interception. It inspected all 33 fixture LEDs in
40 single-tap frames and 106 saturated-cooldown cold frames.

Every tested cold RGBW value had R=0, G=0, W=0. The final lit logical color and
packed PWM data were both `[0,0,1,0]`. Therefore these replayed cold tails do not
contain a commanded red contribution at those tested software boundaries.
This does not establish the behavior of the real wiring, asynchronous physical
SPI transaction, LED-driver state, channel calibration or emitted light.
It does not contradict the user's observed red or prove a hardware cause.

At black the released code lowers the driver-control GPIO without queuing a
zero-PWM frame. The prior blue PWM bytes remain in its software driver buffer.
This is a candidate shutdown-path issue, not a confirmed cause. The earlier
separate resistor-setting experiment is also unconfirmed and was not selected
or flashed in this investigation. No new binary patch is included.

## Consequence for the implementation

r15 ports heatmap math through a different output architecture; it is not a
faithful reproduction of QMK's rendering. A direct per-key RGB output path or
local animation on the halves would remove shared-palette grouping; local
keypress processing would also remove the Neuron round trip. The available
1.5.0 side communications source registers `LED` as an empty handler, so merely
sending that command cannot bypass the palette path. Implementing that path
requires changing the side firmware; the complete Raise 2 side build remains
unavailable in this checkout. The public Dygmalab repository listing and filename
searches were checked again on 2026-11-06 without finding the missing side driver
and wired-communication build sources.

Some Neuron-side improvements may still be possible. These findings do not
prove all three symptoms are unfixable, and another timing/palette adjustment
would not justify claiming that they are resolved. No r16 is packaged from this
audit. Earlier r12/r14 installation artifacts retain their recorded hashes.

## Local reproducible investigation artifacts

Synthetic probes and results are retained under ignored
`build/verification/r15-audit/`: `latency.cpp`, `colors.cpp`, `driver-tail.py`
and `driver-tail.json`. They do not contain captured user keystrokes or profiles.
The production source and all installation images remain unchanged by this audit.
