#!/usr/bin/env python3
"""Run the actual release Hand decoder and matrix scan on captured-format packets."""
from pathlib import Path
import importlib.util
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
release = '3357a69e835622d44f52505e99c961e82e12dbda'

def git(*args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True)

subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor',
                release, 'HEAD'], check=True)
assert git('rev-parse', 'Raise2_v1.5.0^{commit}').strip() == release
for file in ['src/common.h', 'src/keyboard_config.h', 'src/hid_report_descriptor.cpp',
             'src/version/FirmwareVersion.cpp', 'src/version/FirmwareVersion.h']:
    assert (root / file).read_text() == git('show', f'Raise2_v1.5.0:{file}'), file
main = (root / 'src/mainDefy.cpp').read_text()
main = main.replace('#include "SplashHeatmapPlugin.h"\n', '')
main = main.replace('RadioManager, _BleManager, SplashHeatmapLED', 'RadioManager, _BleManager')
assert main == git('show', 'Raise2_v1.5.0:src/mainDefy.cpp'), 'Unexpected stock startup/keymap change'
assert all(s.startswith(' ') for s in git('submodule', 'status', '--recursive').splitlines())
pins = json.loads((root / 'tests/splash/upstream-pins.json').read_text())
allowed = {
    'libraries': {'Communications', 'Kaleidoscope', 'Spi_slave/SpiPort.cpp', '.gitmodules'},
    'libraries/Kaleidoscope': {'src/kaleidoscope/device/dygma/keyboardManager/KeyboardManager.cpp'},
    'libraries/Communications': {'src/Communications.h',
        'src/device/neuron/defyN2/CommunicationsN2.cpp',
        'src/device/neuron/defyN2/CommunicationsN2_side.h',
        'src/device/neuron/common/Communications_side.h',
        'src/device/neuron/common/Communications_side.cpp',
        'src/device/keyScanner/CommunicationsKS.cpp'},
}
for line in git('submodule', 'status', '--recursive').splitlines():
    sha, path = line.split()[:2]
    module = root / path
    if path in allowed:
        subprocess.run(['git', '-C', str(module), 'merge-base', '--is-ancestor',
                        pins[path], 'HEAD'], check=True)
        changed_since_base = subprocess.check_output(['git', '-C', str(module),
            'diff', '--name-only', pins[path]], text=True).splitlines()
        assert set(changed_since_base) <= allowed[path], (path, changed_since_base)
    elif path != 'side-patches':
        assert sha == pins[path], f'Unmodified upstream module moved: {path}'
    changed = subprocess.check_output(['git', '-C', str(root / path),
        'status', '--porcelain'], text=True)
    assert all(row[3:] in allowed.get(path, set()) for row in changed.splitlines()), (path, changed)
subprocess.run([__import__('sys').executable, str(root / 'side-patches/tests/test_patch.py'),
                str(root / 'build/vendor/keyscanner-v1.5.0.bin')], check=True)

scanner = root / 'libraries/Kaleidoscope/src/kaleidoscope/device/dygma/keyboardManager'
source = (scanner / 'KeyboardManager.cpp').read_text()
# Only the two BLE disconnect stores may differ in the cache owner's source.
stock_source = subprocess.check_output(['git', '-C', str(root / 'libraries/Kaleidoscope'),
    'show', pins['libraries/Kaleidoscope'] + ':src/kaleidoscope/device/dygma/keyboardManager/KeyboardManager.cpp'], text=True)
restored = source
for hand, device in [('left', 'LEFT'), ('right', 'RIGHT')]:
    guard = f'if ({hand}Connection[1] == BLE_DEFY_{device}) {hand}Connection[1] = UNKNOWN;'
    assert restored.count(guard) == 1
    restored = restored.replace(guard, f'{hand}Connection[1] = UNKNOWN;')
assert restored == stock_source, 'Unexpected key scanner/cache source changes'
start = source.index('void KeyboardKeyScanner::actOnMatrixScan()')
opening = source.index('{', start)
depth = 1
end = opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
scan_function = source[start:end]

prefix = r'''
#include <cassert>
#include <tuple>
#include <vector>
#include <cstring>
#include <iostream>
#include "Hand.h"
#include "Communications.h"
MockCommunications Communications;
using namespace Communications_protocol;
using namespace kaleidoscope::device::dygma::dygma_keyboards;
static_assert(sizeof(((key_data*)nullptr)->rows) == 5, "Stock side packets are five rows of one byte each");
std::vector<std::tuple<unsigned,unsigned,unsigned>> events;
struct KeyAddr { unsigned row, col; KeyAddr(unsigned r,unsigned c):row(r),col(c){} };
constexpr int Key_NoKey = 0;
#define bitRead(value, bit) (((value) >> (bit)) & 1)
#define PRINT_KEYSWITCH_EVENT_PARAMETERS 0
namespace kaleidoscope { namespace device { namespace dygma {
class KeyboardKeyScanner {
 public:
  struct Props_ { enum {matrix_rows=5, left_columns=8, matrix_columns=16}; };
  using ThisType = KeyboardKeyScanner;
  static key_data leftHandState, rightHandState, previousLeftHandState, previousRightHandState;
  void actOnMatrixScan();
  static void handleKeyswitchEvent(int, KeyAddr a, uint8_t state) {
    events.emplace_back(a.row,a.col,state);
  }
};
key_data KeyboardKeyScanner::leftHandState{}, KeyboardKeyScanner::rightHandState{},
 KeyboardKeyScanner::previousLeftHandState{}, KeyboardKeyScanner::previousRightHandState{};
'''
suffix = r'''
}}}
int main() {
  using kaleidoscope::device::dygma::KeyboardKeyScanner;
  Hand left(Hand::LEFT),right(Hand::RIGHT);
  left.init(); right.init();
  KeyboardKeyScanner scanner;
  for (unsigned side=0;side<2;++side) {
    for (unsigned row=0;row<5;++row) for (unsigned col=0;col<8;++col) {
      Packet packet{};
      packet.header.command=HAS_KEYS;
      packet.header.device=side?KEYSCANNER_DEFY_RIGHT:KEYSCANNER_DEFY_LEFT;
      packet.header.size=5;
      packet.data[row]=1u<<col;
      Packet original=packet;
      Communications.callbacks.call(HAS_KEYS,packet);
      assert(memcmp(&packet,&original,sizeof(packet))==0);
      const key_data &keys=side?right.getKeyData():left.getKeyData();
      for (unsigned r=0;r<5;++r) assert(keys.rows[r]==(r==row?(1u<<col):0));
      KeyboardKeyScanner::leftHandState.all=KeyboardKeyScanner::rightHandState.all=0;
      KeyboardKeyScanner::previousLeftHandState.all=KeyboardKeyScanner::previousRightHandState.all=0;
      (side?KeyboardKeyScanner::rightHandState:KeyboardKeyScanner::leftHandState)=keys;
      events.clear(); scanner.actOnMatrixScan();
      assert(events.size()==1);
      assert(events[0]==std::make_tuple(row,side?15-col:col,2u));
      packet.data[row]=0;
      Communications.callbacks.call(HAS_KEYS,packet);
    }
  }
  std::cout<<"Actual v1.5.0 Hand decoder and matrix scan: all 80 positions passed\n";
}
'''
color_spec = importlib.util.spec_from_file_location('splash_color', root / 'tools/generate-splash-color.py')
color_tool = importlib.util.module_from_spec(color_spec)
color_spec.loader.exec_module(color_tool)
assert (root / 'src/SplashColor.h').read_text() == color_tool.generated_header()
with tempfile.TemporaryDirectory(prefix='raise2-release-tests-') as temp:
    temp = Path(temp)
    (temp / 'actual_color_transfer.inc').write_text('\n'.join(
        'static const uint8_t ' + name + '_transfer[256] = {' + ','.join(map(str, table)) + '};'
        for name, table in zip(('red', 'green'), color_tool.transfer_tables())))
    test = temp / 'decoder.cpp'
    test.write_text(prefix + scan_function + suffix)
    flags = ['g++', '-std=c++11', '-fsanitize=address,undefined', '-g',
             '-Wno-c++17-extensions', '-DARDUINO_ARCH_NRF52', '-DKEYBOARDMANAGER_H',
             '-I' + str(root / 'tests/splash/stubs'), '-I' + str(temp),
             '-I' + str(scanner / 'universalModules'),
             '-I' + str(scanner),
             '-I' + str(root / 'libraries/Communications/src')]
    subprocess.run(flags + [str(test), str(scanner / 'universalModules/Hand.cpp'),
                             '-o', str(temp / 'decoder')], check=True)
    subprocess.run([str(temp / 'decoder')], check=True)
    subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-fsanitize=address,undefined',
                    '-I' + str(root / 'src'), '-I' + str(temp), '-I' + str(root / 'libraries/Communications/src'),
                    str(root / 'tests/splash/model.cpp'), '-o', str(temp / 'model')], check=True)
    fade_trace = temp / 'fade.bin'
    subprocess.run([str(temp / 'model'), str(fade_trace)], check=True)
    jitter_flags = ['g++', '-std=c++11', '-fsanitize=address,undefined', '-g',
                    '-I' + str(temp), '-I' + str(root / 'libraries/Communications/src')]
    subprocess.run(jitter_flags + ['-I' + str(root / 'src'),
        str(root / 'tests/splash/jitter.cpp'), '-o', str(temp / 'jitter')], check=True)
    subprocess.run([str(temp / 'jitter')], check=True)
    legacy = temp / 'legacy'
    legacy.mkdir()
    (legacy / 'SplashHeatmap.h').write_text(git('show', 'ec208d9:src/SplashHeatmap.h'))
    subprocess.run(jitter_flags + ['-I' + str(legacy), '-I' + str(root / 'src'),
        str(root / 'tests/splash/jitter.cpp'), '-o', str(temp / 'legacy_jitter')], check=True)
    subprocess.run([str(temp / 'legacy_jitter'), '--expect-jitter'], check=True)
    subprocess.run(flags + ['-include', str(root / 'tests/splash/stubs/host.h'),
                    '-I' + str(root / 'src'),
                    '-I' + str(root / 'libraries/NeuronLedLibrary/LedModeSerializable/src'),
                    str(root / 'tests/splash/plugin.cpp'), str(root / 'src/SplashHeatmapPlugin.cpp'),
                    '-o', str(temp / 'plugin')], check=True)
    trace = temp / 'maps.bin'
    subprocess.run([str(temp / 'plugin'), str(trace)], check=True)
    communication = root / 'libraries/Communications/src'
    transport_source = (communication / 'device/neuron/defyN2/CommunicationsN2.cpp').read_text()
    begin = transport_source.index('bool Communications::sendPacket(Packet packet)')
    end = transport_source.index('\nbool Communications::sendPacketHostConnection', begin)
    (temp / 'actual_send.inc').write_text(transport_source[begin:end])
    base_flags = ['g++', '-std=c++11', '-fsanitize=address,undefined', '-g']
    callbacks_start = source.index('    Communications.callbacks.bind(CONNECTED, (')
    callbacks_end = source.index('    Communications.callbacks.bind(DISCONNECTED, checkBrightness);', callbacks_start)
    (temp / 'actual_connection_callbacks.inc').write_text(source[callbacks_start:callbacks_end])
    begin = source.index('Communications_protocol::Devices KeyboardKeyScanner::leftHandDevice(')
    end = source.index('\nvoid KeyboardKeyScanner::usbConnectionsStateMachine()', begin)
    (temp / 'actual_connection_getter.inc').write_text(source[begin:end])
    common = communication / 'device/neuron/common'
    crc_source = (root / 'libraries/CRC/CRC_wrapper.cpp').read_text()
    begin = crc_source.index('static constexpr uint8_t crc_table')
    end = crc_source.index('\nuint32_t crc32(', begin)
    (temp / 'actual_crc8.inc').write_text(crc_source[begin:end])
    subprocess.run(base_flags + ['-I' + str(root / 'tests/splash/stubs'), '-I' + str(temp),
        '-I' + str(common), '-I' + str(communication), '-I' + str(temp),
        str(root / 'tests/splash/route_state.cpp'), str(common / 'Communications_side.cpp'),
        str(common / 'Communication_model.cpp'), '-o', str(temp / 'route_state')], check=True)
    subprocess.run([str(temp / 'route_state')], check=True)
    if importlib.util.find_spec('unicorn'):
        disconnect_packets = temp / 'released-disconnects.bin'
        subprocess.run([__import__('sys').executable, str(root / 'tests/splash/side_disconnect.py'),
                        str(disconnect_packets)], check=True)
        subprocess.run([str(temp / 'route_state'), str(disconnect_packets)], check=True)
    # Each protection independently catches a real regression in the old code.
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    def expect_regression(common_source, label, expected_assertion):
        binary = temp / 'route_state_old'
        subprocess.run(base_flags + ['-I' + str(root / 'tests/splash/stubs'), '-I' + str(temp),
            '-I' + str(common), '-I' + str(communication), '-I' + str(temp),
            str(root / 'tests/splash/route_state.cpp'), str(common_source),
            str(common / 'Communication_model.cpp'), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        assert result.returncode != 0 and expected_assertion in result.stderr, (label, result.stderr)
        print(label + ': old implementation fails the regression as expected')
    previous_common = temp / 'ComSide-stock.cpp'
    previous_common.write_text(subprocess.check_output(['git', '-C', str(root / 'libraries/Communications'),
        'show', pins['libraries/Communications'] + ':src/device/neuron/common/Communications_side.cpp'], text=True))
    expect_regression(previous_common, 'Wired notification boundary', 'getter()==wired && link.side.is_connected()')
    (temp / 'actual_connection_callbacks.inc').write_text(stock_source[stock_source.index(
        '    Communications.callbacks.bind(CONNECTED, ('):stock_source.index(
        '    Communications.callbacks.bind(DISCONNECTED, checkBrightness);')])
    expect_regression(common / 'Communications_side.cpp', 'BLE cache ownership', 'cache[0]==UNKNOWN && getter()==wired')
    subprocess.run(base_flags + ['-I' + str(communication), '-I' + str(temp),
        str(root / 'tests/splash/transport.cpp'), '-o', str(temp / 'transport')], check=True)
    subprocess.run([str(temp / 'transport')], check=True)
    spi_source = (root / 'libraries/Spi_slave/SpiPort.cpp').read_text()
    begin = spi_source.index('bool SpiPort::sendPacket(Packet &packet)')
    end = spi_source.index('\nvoid SpiPort::clearSend()', begin)
    (temp / 'actual_spi_send.inc').write_text(spi_source[begin:end])
    stubs = temp / 'nrf_stubs'
    stubs.mkdir()
    for name in ['nrf_drv_spis.h', 'nrf_log.h', 'nrf_log_ctrl.h', 'nrf_log_default_backends.h']:
        (stubs / name).write_text('#pragma once\n')
    subprocess.run(base_flags + ['-include', 'cstring', '-I' + str(stubs), '-I' + str(communication),
        '-I' + str(temp), '-I' + str(root / 'libraries/Fifo_buffer'),
        str(root / 'tests/splash/spi_queue.cpp'), str(root / 'libraries/Fifo_buffer/Fifo_buffer.cpp'),
        '-o', str(temp / 'spi_queue')], check=True)
    subprocess.run([str(temp / 'spi_queue')], check=True)
    subprocess.run(base_flags + ['-DKEYSCANNER', '-I' + str(root / 'tests/splash/side_stubs'),
        '-I' + str(root / 'libraries/NeuronLedLibrary/LedModeSerializable/src'),
        str(root / 'tests/splash/side_render.cpp'), '-o', str(temp / 'side_render')], check=True)
    subprocess.run([str(temp / 'side_render')], check=True)
    print('Actual transmit filter/routing, SPI FIFO overflow/retry, and released one-shot side renderer passed')
    if importlib.util.find_spec('unicorn'):
        spec = importlib.util.spec_from_file_location('side_binary', root / 'tests/splash/side_binary.py')
        side = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(side)
        stock = (root / 'build/vendor/keyscanner-v1.5.0.bin').read_bytes()
        patched = side.patcher.patch(stock)
        side.check(stock, patched)
        subprocess.run([__import__('sys').executable, str(root / 'tests/splash/side_scheduler.py')], check=True)
        subprocess.run([__import__('sys').executable, str(root / 'tests/splash/color_transfer.py'), str(fade_trace)], check=True)
        receivers = [side.Receiver(patched), side.Receiver(patched)]
        packets = trace.read_bytes()
        colors = [set(), set()]
        map_count = palette_count = 0
        for offset in range(0, len(packets), 32):
            p = packets[offset:offset+32]
            receiver = receivers[p[1]-1]
            if p[0] == 23:
                receiver.update_palette(p[4], p[5:4+p[2]])
                palette_count += 1
            elif p[0] in (24, 25):
                ug = p[0] == 25
                values = [(p[5+i//2] >> ((i%2)*4)) & 15 for i in range(54 if ug else 36)]
                assert receiver.update(p[4], ug, values) == 10
                map_count += 1
            elif p[0] == 21 and p[4:8] == bytes.fromhex('f5924724'):
                receiver.set_mode(p[4:4+p[2]])
                colors[p[1]-1].add(receiver.displayed_rgb())
        assert len(colors[0]) >= 35, 'The real side trace still has a fixed handful of RGB colors'
        print(f'Actual RP2040 palette/map/mode trace: {palette_count} palette packets, {map_count} maps, {len(colors[0])} distinct left-key RGB values')
    else:
        print('RP2040 emulation skipped: run with the raise2-toolchain venv (required by build-local.sh)')
print('Stock keymap/startup and HID/configuration files unchanged; declared fork changes descend from pinned 1.5.0 sources; other recursive pins unchanged')
