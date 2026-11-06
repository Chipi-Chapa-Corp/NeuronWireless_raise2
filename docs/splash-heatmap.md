# Raise 2 Splash/Heatmap — revision 7

This fork is based on the exact `Raise2_v1.5.0` release. Revision 7 keeps revision
5's animation and event-driven wake handling, with diagnostic counters/commands
and periodic palette/map refresh removed. The helper provides only on, off and
status. The new Neuron image has passed software checks but has not been tested
on the physical keyboard. No keyboard commands or flashing were performed while
preparing it.

## Animation

- Each tap adds 48/255 energy immediately. Repeated taps add up to saturation.
- Neighbor rings receive half the preceding ring's energy: 24, 12, 6, 3, 1.5,
  and so on in fractional precision. Diagonal neighbors are included. Splash
  stays within each half; thumb keys use an approximate lower visual row.
- Energy decays every 10 ms with an approximately 1.757-second half-life. A single
  tap remains visible for about 10 seconds; a saturated key for about 14 seconds.
- Frames are capped at 50 fps. Each half gets an adaptive RGB palette with up to
  15 nonblack colors. An isolated splash fits exactly; busy overlapping splashes
  may merge nearby energy levels. Temporal dithering is not used.
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

An intermittent left LED freeze was reported on r4 while typing still worked.
The user reported r5 working well without an observed freeze. Its physical cause
has not been confirmed. Removing the refresh does not establish that the problem
is fixed. Software emulation finds burst redraw commands can postpone drawing,
but drawing resumes 10 ms after the burst ends.

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

    build/install/splash-heatmap-v1.5.0-r7

It contains `Wireless_neuron.hex`, `keyscanner.bin`, `SHA256SUMS`, a manifest and
this README. The Neuron is rebuilt from source. The side image is the SAME image
as revisions 2 through 6; already patched halves do not require a different side
image. Bazecor compares side application CRCs during its normal upgrade process.
No automatic flashing is performed by the build scripts.

After installation, close Bazecor's serial connection and run:

```bash
python tools/splash.py on
python tools/splash.py status
python tools/splash.py off
```

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

Tests exercise all 80 stock matrix positions, unchanged keymap/storage object
hashes, splash rings, fractional decay, RGB smoothness, per-layer isolation,
reconnects, restore, FIFO overflow/retry, and the absence of periodic idle writes.
The release RP2040 map handlers execute 10,000 updates in Unicorn without vector
growth. Its palette handler and mode setter receive the actual Neuron packet
trace. A separate test executes the real side timers, scheduler and Layer renderer
with hardware clock, power, float ROM helpers and physical driver substituted.

The package verifies exact RF-host merging, flash ranges, vector table, input and
output side hashes, declared patch-byte boundaries and pinned source revisions.
These checks do not simulate the complete keyboard or prove physical stability,
flashing or recovery. Test ordinary typing, layer switching, colors after off,
reconnects and longer use on hardware before treating a new build as established.
