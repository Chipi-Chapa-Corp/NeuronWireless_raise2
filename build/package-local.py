#!/usr/bin/env python3
"""Verify and package revision 11; never connect to a keyboard."""
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import shutil
import struct
import subprocess
import tempfile
from intelhex import IntelHex

root = Path(__file__).resolve().parents[1]
out = root / 'build/install/splash-heatmap-v1.5.0-r11'
app = IntelHex(str(root / 'build/release/NeuronWireless_raise2.hex'))
final_path = root / 'build/release/NeuronWireless_raise2_final.hex'
final = IntelHex(str(final_path))
rf = IntelHex(str(root / 'libraries/rf_host_device/rf_host_device/bin/rf_host_device.hex'))
assert app.minaddr() == 0x2E000 and app.maxaddr() < 0x70000
assert final.minaddr() == 0x27000 and final.maxaddr() < 0x70000
content = set(app.addresses()) | set(rf.addresses())
assert content <= set(final.addresses())
# The stock merge tool emits 0xff padding in the gap between RF host and app.
padding = set(final.addresses()) - content
assert all(rf.maxaddr() < a < app.minaddr() and final[a] == 0xff for a in padding)
assert all(final[a] == app[a] for a in app.addresses())
assert all(final[a] == rf[a] for a in rf.addresses())
sp, reset = struct.unpack('<II', bytes(app.tobinarray(start=0x2E000, size=8)))
assert 0x20009400 <= sp <= 0x20020000 and reset & 1 and 0x2E000 <= reset < 0x70000
assert b'v1.5.0-splash11' in bytes(app.tobinarray())
for removed in (b'splash.stats', b'splash.transport', b'splash.refresh'):
    assert removed not in bytes(app.tobinarray()), 'Removed diagnostic command remains in firmware'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

baseline = json.loads((root / 'tests/splash/stock-baseline.json').read_text())
for name, expected in baseline['critical_objects'].items():
    if name == 'KeyboardManager.o':
        continue  # Only its disconnect callback is allowed to change, checked below.
    assert sha(root / 'build/release/obj' / name) == expected, f'Stock object changed: {name}'
spec = importlib.util.spec_from_file_location('verify_keyboard_manager', root / 'tests/splash/verify_keyboard_manager.py')
manager_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manager_check)
manager_validation = manager_check.verify(root / 'build/release/obj/KeyboardManager.o')
print(f"KeyboardManager: {manager_validation['stock_text_sections_identical']} stock text sections identical; only disconnect callback changed")
with tempfile.TemporaryDirectory() as tmp:
    keymap = Path(tmp) / 'keymap.bin'
    subprocess.run(['arm-none-eabi-objcopy', '-O', 'binary', '--only-section=.rodata.keymaps_linear',
        str(root / 'build/release/obj/mainDefy.o'), str(keymap)], check=True)
    assert len(keymap.read_bytes()) == baseline['default_keymap_size']
    assert sha(keymap) == baseline['default_keymap_sha256']

spec = importlib.util.spec_from_file_location('side_patch', root / 'tools/patch-side-led-maps.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
stock = (root / 'build/vendor/keyscanner-v1.5.0.bin').read_bytes()
side = patcher.patch(stock)
side_sha256 = hashlib.sha256(side).hexdigest()
assert side_sha256 == '1bf7b886df75ef50ff0a0f5a410a3d53ae0c40bfa96aedd2e3e607da2ad0ebc7', 'Animation tuning must not change the working r2 side image'
tests_log = (root / 'build/verification/tests.log').read_text()
rgb_test = re.search(r'Actual plugin post-gamma RGB trace: (\d+) distinct single-tap colors', tests_log)
assert rgb_test and int(rgb_test.group(1)) >= 30, 'Post-gamma RGB fade evidence missing'
hot_test = re.search(r'Actual repeated-tap post-gamma RGB trace: (\d+) colors', tests_log)
assert hot_test and int(hot_test.group(1)) >= 150, 'Repeated-tap post-gamma fade evidence missing'
assert 'Actual RP2040 scheduler/timers/Layer renderer: 33/36 LEDs' in tests_log
assert 'Released side radio disconnect packets: valid CRC, no false wired disconnect, continuing keys and cached LED routing passed' in tests_log
assert 'BLE cache ownership: old implementation fails the regression as expected' in tests_log
assert 'Wired notification boundary: old implementation fails the regression as expected' in tests_log
assert 'Dim tail model: every final distinct green holds 250 ms' in tests_log
assert 'Actual plugin dim-tail packet timing: five equal holds within 250 +/- 30 ms' in tests_log
assert 'Actual RP2040 single-tap fade: 32 output colors; dim tail holds 240-260 ms; final black' in tests_log
assert 'Actual RP2040 overlapping-splash fade: every key matches post-gamma RGB, no red tail, final black' in tests_log
# Prepare all bytes only after image and stock-input/storage checks have passed.
out.mkdir(parents=True, exist_ok=True)
(out / 'keyscanner.bin').write_bytes(side)
shutil.copyfile(final_path, out / 'Wireless_neuron.hex')
shutil.copyfile(root / 'docs/splash-heatmap.md', out / 'README.txt')
for name in ('left-led-freeze.md', 'repositories.md', 'fade-stepping.md'):
    shutil.copyfile(root / 'docs' / name, out / name)
hashes = {f: sha(out / f) for f in ['Wireless_neuron.hex', 'keyscanner.bin']}
(out / 'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name, digest in hashes.items()))
manifest = {
    'base_tag': 'Raise2_v1.5.0',
    'base_commit': '3357a69e835622d44f52505e99c961e82e12dbda',
    'source_commit': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
    'submodules': {line.split()[1]:line.split()[0] for line in
        subprocess.check_output(['git', '-C', str(root), 'submodule', 'status', '--recursive'], text=True).splitlines()},
    'custom_version': 'v1.5.0-splash11',
    'hardware_tested': False,
    'hardware_recovery_tested': False,
    'previous_revision_hardware_report': 'r5: left LEDs frozen, right active; live status shows left route UNKNOWN, 44065 queued frames unchanged, last frame about 12 minutes old despite newer left key packets.',
    'observed_neuron_route_failure_confirmed': True,
    'event_that_cleared_scanner_route_confirmed': False,
    'previous_revision_user_reported_working': 'v1.5.0-splash2',
    'installation_recommendation': 'Fix candidate for observed stale scanner route: suppress relabeled radio disconnects at the wired receiver and preserve wired cache entries on BLE disconnect. Splash uses the normal scanner cache. No periodic refresh or diagnostic counters. Gamma/channel compensation and equal 250 ms dim-color holds address reproduced stepping. New Neuron image is not hardware-tested; side binary unchanged.',
    'side_identical_to_revision_2': True,
    'palette_strategy': 'Per-frame adaptive palette, up to 15 nonblack energy groups per half; inverse released gamma/RG gains; no temporal dithering',
    'requires_patched_firmware_on_both_halves': True,
    'side_built_from_source': False,
    'side_method': 'Exact stock application binary patch: two four-byte instruction windows plus CRC fields',
    'stock_side_sha256': patcher.STOCK_SHA256,
    'side_program_crc': hex(struct.unpack_from('<I', side, 20)[0]),
    'side_seal_crc': hex(struct.unpack_from('<I', side, 8)[0]),
    'side_instruction_patches': [{'address':hex(a),'file_offset':hex(a-patcher.IMAGE_BIAS),
        'before':old.hex(),'after':new.hex()} for a,old,new in patcher.PATCHES],
    'files_sha256': hashes,
    'stock_key_decoder_and_storage_objects_identical': {k:v for k,v in baseline['critical_objects'].items() if k != 'KeyboardManager.o'},
    'keyboard_manager_validation': manager_validation,
    'stock_default_keymap_sha256': baseline['default_keymap_sha256'],
    'validation': {
        'matrix_positions':80, 'emulated_map_updates':10000,
        'emulated_same_mode_rearms':100,
        'exclusive_output_layer_mode_palette_reconnect_and_restore_tests':True,
        'full_hardware_or_flashing_test':False,
        'tap_boost':48, 'neighbor_falloff_per_ring':0.5,
        'frame_cap_fps':50, 'high_energy_decay_half_life_ms_approx':1757,
        'dim_tail_distinct_color_hold_ms':250,
        'actual_renderer_single_tap_colors':32,
        'actual_renderer_saturated_colors':169,
        'actual_renderer_dim_tail_hold_ms_before_global_brightness':[240,260],
        'actual_gamma_transfer_all_256_red_green_inputs_verified':True,
        'physical_red_tail_cause_confirmed':False,
        'adaptive_palette_and_monotone_rgb_fade_tests':True,
        'distinct_single_tap_post_gamma_rgb_colors':int(rgb_test.group(1)),
        'distinct_repeated_tap_post_gamma_rgb_colors':int(hot_test.group(1)),
        'periodic_palette_map_refresh_ms':0,
        'diagnostic_counters_and_commands':False,
        'stale_pending_frame_timeout_ms':1000,
        'wake_mode_request_host_resume_and_one_sided_stall_tests':True,
        'actual_spi_fifo_overflow_and_retry_test':True,
        'normal_scanner_cache_routing_restored':True,
        'released_radio_disconnect_packets_preserve_wired_cache':True,
        'ble_disconnect_preserves_distinct_wired_slot':True,
        'genuine_disconnect_reconnect_and_key_release_both_halves':True,
        'both_protections_fail_regression_with_old_code':True,
        'actual_rp2040_scheduler_timer_layer_renderer_test':True,
    },
    'neuron_flash_range': [hex(final.minaddr()), hex(final.maxaddr())],
    'source_sha256': {str(p.relative_to(root)):sha(p) for p in [
        root/'src/SplashHeatmap.h',root/'src/SplashColor.h',root/'tools/generate-splash-color.py',root/'src/SplashHeatmapPlugin.cpp',root/'src/SplashHeatmapPlugin.h',
        root/'src/mainDefy.cpp',root/'libraries/Kaleidoscope/src/kaleidoscope/device/dygma/keyboardManager/KeyboardManager.cpp',root/'libraries/Spi_slave/SpiPort.cpp',root/'libraries/Communications/src/Communications.h',
        root/'libraries/Communications/src/device/neuron/defyN2/CommunicationsN2.cpp',
        root/'libraries/Communications/src/device/neuron/defyN2/CommunicationsN2_side.h',
        root/'libraries/Communications/src/device/neuron/common/Communications_side.cpp',
        root/'libraries/Communications/src/device/neuron/common/Communications_side.h',
        root/'libraries/Communications/src/device/keyScanner/CommunicationsKS.cpp',
        root/'tools/patch-side-led-maps.py',root/'tools/splash.py',
        root/'side-patches/tools/patch-side-led-maps.py',root/'side-patches/patches/raise2-v1.5.0.json']},
}
(out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Verified Splash build (no diagnostics or periodic refresh; Neuron image not hardware-tested):', out)
print('Side binary is identical to r2; existing patched halves need no new side firmware. No hardware was flashed.')
