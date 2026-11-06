# Left LED freeze: captured failure and notification fix

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

The stock `ComSide` state machine can remain connected while delivering a
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

Before the fix, feeding these packets through the real Neuron ComSide and stock
cache callbacks reproduced UNKNOWN with continuing input. Revision 9 replays both
packets, checks their CRCs using the actual Neuron CRC8 code, and verifies that
no false wired DISCONNECTED reaches application callbacks. The cached wired
route remains valid and ten subsequent key packets arrive on each half.

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

## Revision 9: fix notification handling

Revision 8 routed Splash directly through the active transport, bypassing the
invalid cache. Revision 9 removes that workaround and its getter APIs. Splash
uses the normal scanner cache again. Two source changes protect that cache:

1. In the Neuron's ComSide connected-state receiver, a DISCONNECTED packet naming
   the same live wired peer is suppressed before reaching global callbacks.
   The released side forwards secondary radio disconnects under that wired ID;
   the packet is not an authoritative wired transport-loss event. Actual cable
   loss, an explicit local disconnect, or model-requested reconnect still uses
   ComSide's local disconnect state, which emits the normal notification.
   RF/BLE peer notifications retain their existing behavior.
2. In Kaleidoscope's connection-cache owner, BLE disconnects clear the SPI slot
   only if that slot holds that BLE device. Previously they cleared the slot
   unconditionally, including when it held a distinct working wired connection.
   BLE-host mode, which intentionally stores a BLE route in that slot, still
   clears it correctly.

This fixes notification handling for all callback consumers, including normal
lighting and key-release logic. A false wired notice no longer releases held
keys or triggers the global disconnect callbacks. A genuine loss still does.
There is no new cache, periodic resend, diagnostic counter, or side-binary patch.

The regressions cover both halves, actual released packets, unrewritten BLE/RF
notices, cable loss, explicit disconnect, model-requested reconnect, peer restart,
and continued key delivery and cached LED routing. Compiling each old protection
in turn makes the corresponding assertion fail. Animation, ownership, packet,
FIFO and side-instruction tests also pass.

The Kaleidoscope source difference is restricted to two conditional cache stores.
All 80 matrix positions retain the stock decoder/scan behavior. The package
requires Hand and EEPROM object hashes and the default keymap to match stock.
KeyboardManager's 84 compiled text sections are checked individually: only the
disconnect callback may change; the other 83 must remain byte-identical.

The side image is identical to revisions 2–8. Revision 9 still needs physical
observation; passing this specific reproduction does not prove that every possible
future LED failure has been eliminated. The side's forwarding behavior remains
in its released binary, but the Neuron no longer interprets that notification as
a wired transport loss. No further binary patch is needed for this fix.
