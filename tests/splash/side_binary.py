#!/usr/bin/env python3
"""Execute the released and patched RP2040 map handlers, without any hardware.

Uses actual application instructions, with only libc memory helpers replaced.
Stops on allocation, range errors, invalid memory, or unexpected control flow.
"""
from pathlib import Path
import importlib.util
import struct
import sys
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('side_patch', root / 'tools/patch-side-led-maps.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


class Receiver:
    vector, heap, packet, stop = 0x200029EC, 0x20010000, 0x20030000, 0x10004000

    def __init__(self, image, layers=10):
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
        self.cpu.mem_map(0x10000000, 0x80000)
        self.cpu.mem_map(0x20000000, 0x42000)
        self.cpu.mem_write(patcher.IMAGE_BIAS, image)
        self.cpu.mem_write(self.heap, bytes([0xA5]) * 9000)
        self.cpu.mem_write(self.vector, struct.pack('<3I', self.heap, self.heap + layers * 90, self.heap + 9000))
        # Only the two supported modes are needed. Use their actual released
        # vtables/deSerializers and the real set_led_mode registration table.
        self.layer_mode, self.solid_mode = 0x200026D8, 0x200027B4
        self.cpu.mem_write(self.layer_mode, struct.pack('<II', 0x10026734, 0x244792F5) + bytes(8))
        self.cpu.mem_write(self.solid_mode, struct.pack('<II', 0x1002670C, 0x8C245E46) + bytes(8))
        self.cpu.mem_write(0x20001594, struct.pack('<10I', self.layer_mode, *([self.solid_mode] * 9)))
        self.cpu.mem_write(0x200029CC, struct.pack('<I', self.layer_mode))
        self.cpu.hook_add(UC_HOOK_CODE, self.hook)
        self.allocations = 0

    def hook(self, cpu, address, size, user):
        if address == self.stop:
            cpu.emu_stop()
        elif address in (0x1000FA2C, 0x1000FA20):
            dst, src, count = [cpu.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2)]
            assert count <= 128
            data = bytes(cpu.mem_read(src, count)) if address == 0x1000FA2C else bytes([src & 255]) * count
            cpu.mem_write(dst, data)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        elif address == 0x10015B90:
            self.allocations += 1
            raise AssertionError('Receiver tried to reallocate its layer vector')
        elif address == 0x1001BBAC:
            raise IndexError('Actual side bounds check rejected the layer')
        elif address in (0x10008734, 0x10014168, 0x10014190, 0x10010C2C):
            # Timer setup, LED power/brightness handlers: hardware is not emulated.
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def set_mode(self, payload):
        self.cpu.mem_write(self.packet, payload.ljust(32, b'\0'))
        self.cpu.reg_write(UC_ARM_REG_SP, 0x20041000)
        self.cpu.reg_write(UC_ARM_REG_R0, self.packet)
        self.cpu.reg_write(UC_ARM_REG_LR, self.stop | 1)
        self.cpu.emu_start(0x100126F5, 0, count=5000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == self.stop, 'Mode setter did not return'

    def update_palette(self, first, colors):
        assert len(colors) % 4 == 0 and len(colors) <= 24 and first + len(colors)//4 <= 16
        packet = bytes([23, 1, 1 + len(colors), 0, first]) + colors
        self.cpu.mem_write(self.packet, packet.ljust(32, b'\0'))
        self.cpu.reg_write(UC_ARM_REG_SP, 0x20041000)
        self.cpu.reg_write(UC_ARM_REG_R0, 0)
        self.cpu.reg_write(UC_ARM_REG_R1, self.packet)
        self.cpu.reg_write(UC_ARM_REG_LR, self.stop | 1)
        self.cpu.emu_start(0x10015675, 0, count=1000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == self.stop, 'Palette handler did not return'

    def displayed_rgb(self, layer=0, led=8):
        # The released Layer renderer copies palette[map[led]] to its RGB buffer;
        # the actual C++ renderer is also compiled by side_render.cpp.
        index = self.cpu.mem_read(self.heap + layer*90 + led, 1)[0]
        assert index < 16
        return bytes(self.cpu.mem_read(0x200029F8 + index*4, 4))

    def update(self, layer, underglow, values):
        count = 54 if underglow else 36
        assert len(values) == count
        data = bytes(values[i] | (values[i+1] << 4) for i in range(0, count, 2))
        packet = bytes([25 if underglow else 24, 1, 1 + len(data), 0, layer]) + data
        self.cpu.mem_write(self.packet, packet.ljust(32, b'\0'))
        self.cpu.reg_write(UC_ARM_REG_SP, 0x20041000)
        self.cpu.reg_write(UC_ARM_REG_R0, 0)
        self.cpu.reg_write(UC_ARM_REG_R1, self.packet)
        self.cpu.reg_write(UC_ARM_REG_LR, self.stop | 1)
        self.cpu.emu_start((0x10015CA4 if underglow else 0x10015DA0) | 1, 0, count=3000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == self.stop, 'Handler did not return'
        begin, end, capacity = struct.unpack('<3I', self.cpu.mem_read(self.vector, 12))
        return (end - begin) // 90


def check(stock, patched, iterations=10000):
    # Demonstrate the fault in the shipped instructions, not just public source.
    broken = Receiver(stock)
    for n in range(50):
        assert broken.update(0, n % 2 == 1, [n % 16] * (54 if n % 2 else 36)) == 11 + n
    fixed = Receiver(patched)
    for n in range(iterations):
        layer, ug = n % 10, bool(n % 2)
        values = [(n + i) % 16 for i in range(54 if ug else 36)]
        before = bytes(fixed.cpu.mem_read(fixed.heap, 9000))
        assert fixed.update(layer, ug, values) == 10
        offset = layer * 90 + (36 if ug else 0)
        expected = bytearray(before)
        expected[offset:offset + len(values)] = bytes(values)
        assert bytes(fixed.cpu.mem_read(fixed.heap, 9000)) == expected, 'Wrong LEDs or adjacent memory changed'
    assert fixed.allocations == 0
    for first, count in ((0, 6), (6, 6), (12, 4)):
        before = bytearray(fixed.cpu.mem_read(0x200029F8, 64))
        colors = bytes((first*4+i)%256 for i in range(count*4))
        fixed.update_palette(first, colors)
        before[first*4:(first+count)*4] = colors
        assert bytes(fixed.cpu.mem_read(0x200029F8, 64)) == before
    # Most importantly, repeated MODE_LED for the SAME layer mode must call the
    # actual released deSerializer and re-arm its one-shot delay every time.
    layer_payload = struct.pack('<IHBBBB', 0x244792F5, 0, 255, 10, 0, 0)
    for _ in range(100):
        fixed.cpu.mem_write(fixed.layer_mode + 11, b'\0')
        fixed.set_mode(layer_payload)
        assert bytes(fixed.cpu.mem_read(fixed.layer_mode + 11, 3)) == bytes([10, 0, 0])
    for layer in (10, 255):
        try:
            fixed.update(layer, False, [0] * 36)
        except IndexError:
            pass
        else:
            raise AssertionError('Out-of-range protection was lost')
    print(f'Actual RP2040 handlers: stock 10 -> 60 layers in 50 updates; patched {iterations} updates retain 10 layers, exact colors and bounds checks')
    print('Actual RP2040 mode setter and deSerializer: 100 repeated same-mode commands re-arm redraw')
    print('Actual RP2040 palette handler: exact RG BW chunk writes passed')


if __name__ == '__main__':
    stock = Path(sys.argv[1]).read_bytes()
    check(stock, patcher.patch(stock))
