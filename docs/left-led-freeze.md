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
there was no event trace before the failure. The next section establishes that
the released side firmware can generate this packet during a radio disconnect.

## Where the false wired disconnect comes from

Further instruction emulation reproduces this sequence in the released 1.5.0
side application itself:

1. The secondary radio peer disconnects while the wired link remains connected.
2. The radio-disconnect routine dispatches DISCONNECTED through the side's shared
   application callbacks. Its original packet identifies BLE-left (9) or
   BLE-right (10).
3. The callback registered by wired initialization forwards that notification
   through the normal Communications sender while wired is up and radio is down.
4. The wired sender overwrites the source device with wired-left (1) or wired-right
   (2), computes a valid CRC8 and queues the packet. It retains the wired-connected
   flag throughout.
5. The Neuron delivers the packet to its stock disconnect callbacks. They clear
   the wired cache entry while the real wired ComSide still receives keys.

This is a transport-identity error: a secondary wireless-link notification is
turned into a wired-half disconnect. A wired reconnect is not implied by the
radio event, so there need not be a fresh wired CONNECTED callback to repair the
cache. The left and right paths reproduce independently; a left event leaves
the right cache intact.

For review, these are application execution addresses in the exact released
binary, using the existing file-to-execution bias of 0x10005000:

| Released code/data | Address |
| --- | --- |
| Radio event handler; event 0x0F selects disconnect | 0x1001A4E8 |
| Radio disconnect routine, notify enabled | 0x1001A418 |
| Wired initialization installs the DISCONNECTED callback | 0x1001B65E–0x1001B710 |
| Forwarding callback | 0x1001AE9C |
| Communications sender | 0x10015B54 |
| Wired sender that rewrites the device and computes CRC8 | 0x1001B508 |
| Wired / radio established flags | 0x2000B723 / 0x2000B71C |

`tests/splash/side_disconnect.py` executes that event handler, callback and sender
chain, substituting only hardware radio stop, scheduler yield, libc memory copy,
display effect and final queue insertion. The callback table uses the layout and
function entry installed by the released initializer. It verifies these exact
32-byte packets (remaining bytes are zero):

- Left: `03 01 00 51 ...` — DISCONNECTED, wired-left, no payload, valid CRC8.
- Right: `03 02 00 ec ...` — DISCONNECTED, wired-right, no payload, valid CRC8.

The generated left packet is then fed through the real Neuron ComSide and stock
cache callbacks. The host test checks its CRC using the actual Neuron CRC8 code,
observes the cache become UNKNOWN, and delivers ten subsequent key packets on
the same still-connected transport.

Both the untouched official side image and our map-patched image produce exactly
the same packets. None of the map patch's instruction windows is involved. This
mechanism is in the released application, rather than introduced by our two map
allocation changes. No new firmware patch or runtime diagnostics were added for
this investigation.

**Evidence limit:** this establishes and tests a specific upstream mechanism
that produces the captured state. It does not retrospectively prove that event
0x0F occurred immediately before the user's incident, nor explain what caused
the secondary wireless connection to drop. That last attribution would require
a connection-event capture spanning a recurrence. The r5 snapshots contained
current state and counters, not a historical event log.

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
