# Left LED freeze: captured failure and route-selection fix

On 2026-11-06 the user reported r5's left lighting frozen with the same keys dimly
green/red while the right kept animating. The installed build included periodic
refresh. The keyboard was left frozen and Bazecor closed. Only the existing r5
`splash.protocol`, `splash2` status, and `splash.stats` queries were sent; no mode,
refresh, configuration, reset or flashing commands were issued.

## Live evidence

Ten snapshots, one second apart, showed:

| Observation | Left | Right |
| --- | --- | --- |
| Overlay / host / LEDs enabled | Yes | Yes |
| Destination | UNKNOWN (0) | Wired right (2) |
| Setup stage | 0 | 25 (complete) |
| Queued frames | 44,065, unchanged | 60,584 → 60,594 |
| Enqueue failures | 0 | 0 |
| Time since last queued frame, first sample | 723,061 ms | 24 ms |
| Time since last key packet, first sample | 13,092 ms | 12,841 ms |
| Periodic resync count | 1,568, unchanged | 2,289 → 2,298 |

The left had received a key packet well after its LED frames stopped. Its LEDs
were not receiving new frames from Splash because the Neuron sender treated
UNKNOWN as disconnected and skipped that half. The periodic refresh lived inside
the same skipped branch, so it could not recover this state. These observations
establish a Neuron-side routing failure; they do not establish a physical driver
lockup or the event that originally cleared the route cache.

Raw snapshots are retained locally under ignored `build/verification`; they are
not required by the firmware or published as runtime telemetry.

## Why input can continue

`KeyboardHands::setup()` maintains `leftConnection` / `rightConnection` using
CONNECTED and DISCONNECTED callbacks. `KeyboardKeyScanner::leftHandDevice()`
reads that list. `Hand` separately consumes HAS_KEYS packets, without requiring
the list to contain a destination. Splash originally used the scanner list as
its authoritative LED route.

The actual `ComSide` state machine can remain connected while delivering a
DISCONNECTED packet to application callbacks. Its connected-state packet handler
forwards that notification without transitioning the transport state. A test
executes that allowed sequence through the real ComSide/ComModel code and stock
connection callbacks: the scanner route becomes UNKNOWN, then ten HAS_KEYS
packets still arrive. This reproduces the cache/transport disagreement. It is
not evidence that this exact packet sequence triggered the physical incident;
there was no event trace before the failure.

## Revision 8

Communications now exposes the destination of its active connected transport.
Splash uses that destination for left/right LED output, rather than the scanner's
event-maintained list. The getter requires a real connected ComSide/model and
returns UNKNOWN for a genuine disconnect. Wired and RF destinations are kept;
BLE forwarding through the other half's bridge is supported.

There is no new cache, periodic resend, diagnostic counter, or side-binary patch.
Normal route changes still initialize the new half's LED configuration. The
stock key decoder, matrix scan, keymap and EEPROM objects remain unchanged.

Tests cover UNKNOWN and stale known scanner destinations while the live transport
remains usable, real disconnect/reconnect, wired/RF/BLE selection, and continued
LED frames. The new regression fails against the revision-7 sender and passes
against revision 8. Existing animation, ownership, packet and side-instruction
tests continue to pass.

The side image is identical to revisions 2–7. Revision 8's new Neuron build still
needs hardware observation; passing the reproduced failure does not prove that
all possible future LED failures are eliminated.
