# Raise 2 Splash/Heatmap — revision 14

This fork is based on the exact `Raise2_v1.5.0` release. Revision 14 replaces
r13's one-second quadratic ending with a 500 ms linear Q8 tail, updated every
10 ms below about 16/255 energy. It does not slow further near black.

The final faint-green output levels 7, 6, 4, 3 and 1 are removed: rounded energy
0..8 produces black. At full global brightness, the final lit green level is 9
for 40 ms in the renderer replay (30 ms in the plugin's paced packet test),
then off. This deliberately trades a larger final cutoff for no lingering dim
green. A single-press center turns off at 2.92 s, compared with r13's 3.50 s;
a saturated center turns off at 7.14 s, compared with 7.72 s.

The cutoff also hides very faint outer rings until more heat accumulates.
Fractional heat remains available for additive presses. Gamma compensation,
the longer high-energy exponential decay and stable palette groups remain.
The red tint's physical cause is unconfirmed: avoiding the reported phase is
not proof of a hardware fix. The ordinary r14 package uses the same map-only
side image as r13. The separate anti-ghost experiment is not included. See
[the fade investigation](fade-stepping.md) and
[the driver investigation](tail-driver-investigation.md).

Revision 9's notification fixes are retained: forwarded wireless disconnects
cannot erase a live wired route. Splash uses the normal scanner connection cache;
revision 8's bypass APIs remain removed. There are no diagnostic counters,
periodic refreshes, temporal dithering, or additional side binary changes.

The new Neuron image has passed software checks but has not been tested on the
physical keyboard. Existing r5 status was read while the real fault was present;
no LED commands, resets or flashing were performed. See [the captured evidence
and regression tests](left-led-freeze.md).

## Animation

- Each tap adds 48/255 energy immediately. Repeated taps add up to saturation.
- Neighbor rings receive half the preceding ring's energy: 24, 12, 6, 3, 1.5,
  and so on in fractional precision. Diagonal neighbors are included. Splash
  stays within each half; thumb keys use an approximate lower visual row.
- Energy decays every 10 ms with an approximately 1.757-second half-life above
  about 16/255. Below that, energy follows a 500 ms linear tail in Q8 precision.
  Rounded energies 0..8 are black, skipping the five faintest green outputs. Integer RGB/PWM output can still repeat between changes.
  Global brightness can merge physical levels; timing tests cover output before
  that final scaling.
- Frames are capped at 50 fps. Each half gets an adaptive RGB palette with up to
  15 nonblack colors. An isolated splash fits exactly; busy overlapping splashes
  may merge nearby energy levels. Their group memberships remain fixed during
  uninterrupted decay. Temporal dithering is not used.
- The Neuron observes the stock five-byte key packets without changing input.
  Per-layer Bazecor colors and mode changes are filtered while Splash owns LEDs.
  Turning Splash off restores all saved maps, the palette and selected stock mode.
  Global brightness and normal power controls retain their existing behavior.

## Output and reconnects

Each frame sends changed palette chunks, its matching map if needed, and a
MODE_LED redraw command. The side Layer renderer is one-shot, so palette/map
writes alone are insufficient. Each half queues at most one overlay packet per
main-loop iteration. SPI FIFO backpressure is propagated to the sender.

There is no periodic resend, no diagnostic telemetry, and no refresh command.
Reconnection, route changes, host resume and actual MODE_LED/RETRY_LAYERS requests
still reinitialize the affected LED configuration. These are state transitions,
not periodic recovery. A frame blocked from enqueueing for a second is discarded
so a recovered transport sends current heat instead of an old bright snapshot.
One unavailable half does not block its peer.

On 2026-11-06 the freeze recurred on r5 despite its periodic refresh. Captured
status showed the left route was UNKNOWN and its queued-frame count had stopped
for about 12 minutes, while newer left input packets arrived and the right kept
updating. The refresh also skipped UNKNOWN routes. Tests execute the released
side's radio disconnect handler and wired forwarding path: the wireless notice
is relabeled as a wired disconnect despite the wired link staying connected.
Revision 9 rejects that false wired lifecycle notification before application
callbacks can invalidate the cache. The exact event preceding the physical
incident was not captured; hardware confirmation of this fix is still pending.

## Build and install

The repository graph and clone command are documented in [repositories.md](repositories.md).
Install the upstream Nordic SDK into `libraries/SDK/nRF5_SDK_17.1.0_ddde560`.
Use GNU Arm Embedded 10.3-2021.10, a host C++ compiler with ASan/UBSan, and Python
with `intelhex` and `unicorn`. The build script expects the compiler directory
and Python venv beneath `~/.local/opt/raise2-toolchain`; override that location
with `RAISE2_TOOLCHAIN_ROOT`.

From the main repository:

```bash
bash build/build-local.sh
```

The script runs input, animation, ownership, transport and side-instruction tests,
then builds and verifies the installable package. Logs are in `build/verification`.
Select this exact folder in Bazecor's custom firmware folder picker:

    build/install/splash-heatmap-v1.5.0-r14

It contains `Wireless_neuron.hex`, `keyscanner.bin`, `SHA256SUMS`, a manifest and
this README. The Neuron is rebuilt from source. The side image is the SAME image
as revisions 2 through 13; already patched halves do not require a different side
image. Bazecor compares side application CRCs during its normal upgrade process.
No automatic flashing is performed by the build scripts.

After flashing, ensure Bazecor has restored your saved configuration and verify
your usual key actions before enabling Splash. A completed firmware upload does
not establish that profile restoration succeeded. An observed Bazecor reconnect
failure left all custom keys erased and selected the built-in layout; turning
Splash off could not repair that state. See [the profile-loss investigation](input-profile-loss.md).

After installation, close Bazecor's serial connection and run:

```bash
python tools/splash.py on
python tools/splash.py status
python tools/splash.py off
```

To check the input configuration against a known-good Bazecor backup before
enabling Splash, use:

```bash
python tools/splash.py on --verify-backup /path/to/your/Bazecor-backup.json
```

This compares the saved keymap, custom-layer selection, default layer, superkeys
and macros. A mismatch stops this invocation before it enables Splash. It does
not restore or change those settings; `off` remains available independently.

The helper sends a command and exits. Animation runs on the keyboard's Neuron.
Splash starts disabled after a power cycle. The helper checks `splash.protocol == 2`
and cannot verify the installed side application's hash over this serial protocol.

## Side application patch and rollback

The full side application project has not been located, so the halves are not
built from source. The pinned [raise2-side-patches](../side-patches/README.md)
repository owns the patcher and its machine-readable metadata. It accepts only
the exact official 1.5.0 image, replaces two four-byte instruction sequences that
append layers during map updates, then recomputes program and seal CRC32 values.
All other bytes, image length, addresses and reset vectors remain unchanged.
The generated patched image SHA256 is:

    1bf7b886df75ef50ff0a0f5a410a3d53ae0c40bfa96aedd2e3e607da2ad0ebc7

The original input SHA256 is:

    d01083b72600f77226f815fcc0828dcc08fb215dd2a6fa12a5fb88890a940186

Stock halves retain the allocation defect and must not be used for this streaming
effect. Narrow binary changes and passing tests do not establish every physical
recovery path. Keep a Bazecor configuration export and an official 1.5.0 rollback
package. On the development workstation, original rollback files and earlier
custom packages remain in ignored `build/rollback` and `build/install` directories.
They are intentionally not published in Git.

## Verification limits

Tests replay the released radio-disconnect packets through the real ComSide
state machine and connection callbacks, verifying that wired routing and key
input continue on both halves. BLE/RF notifications, genuine cable loss,
reconnect, peer restart and key releases are covered. Each protection's regression
fails with its original implementation. Tests also exercise all 80 stock matrix
positions, unchanged Hand/EEPROM objects and default keymap. In KeyboardManager,
only the compiled disconnect callback may differ: the other 83 text sections
must match stock exactly. Tests cover splash rings, fractional decay, RGB
smoothness, per-layer isolation,
reconnects, restore, FIFO overflow/retry, and the absence of periodic idle writes.
The release RP2040 map handlers execute 10,000 updates in Unicorn without vector
growth. Its palette handler and mode setter receive the actual Neuron packet
trace. A separate test executes the real side timers, scheduler and Layer renderer
with hardware clock, power, float ROM helpers and physical driver substituted.
All 256 R/G inputs verify the compensation oracle against the actual Layer
renderer. Isolated, saturated and overlapping fades replay through that renderer
and compare all keys' post-gamma output with the expected values. Global
brightness scaling and physical PWM/LED behavior remain hardware limits.

The package verifies exact RF-host merging, flash ranges, vector table, input and
output side hashes, declared patch-byte boundaries and pinned source revisions.
These checks do not simulate the complete keyboard or prove physical stability,
flashing or recovery. Test ordinary typing, layer switching, colors after off,
reconnects and longer use on hardware before treating a new build as established.
