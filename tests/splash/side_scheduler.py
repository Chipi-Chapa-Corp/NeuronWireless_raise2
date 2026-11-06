#!/usr/bin/env python3
"""Investigate the released LED scheduler/renderer without touching hardware.

Executes the stock timer set/check, mode setter, Layer renderer and scheduler.
Only bottom-level clock/IRQ/alarm, floating-point ROM operations, debug output,
power controls and physical driver flush are substituted. This cannot establish
what happened to a physical half's transport or LED driver.
"""
from pathlib import Path
import struct
from side_binary import Receiver, patcher
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def floating(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


class ScheduledReceiver(Receiver):
    def __init__(self, image, leds):
        super().__init__(image)
        self.now = 0
        self.draws = []
        self.flushes = []
        self.cpu.mem_write(self.heap, bytes(9000))
        self.cpu.mem_write(0x200029B0, struct.pack('<I', 0x20031000))
        self.cpu.mem_write(0x200029D4, struct.pack('<I', self.layer_mode))
        self.cpu.mem_write(0x200029D8 + 12, bytes([leds - 1]))
        self.cpu.mem_write(0x200029BC, bytes([1] * 5))
        for guard in (0x200026AC, 0x200026B0, 0x200026B4, 0x200026B8):
            self.cpu.mem_write(guard, b'\1')

    def hook(self, cpu, address, size, user):
        def ret(value=None):
            if value is not None:
                cpu.reg_write(UC_ARM_REG_R0, value & 0xFFFFFFFF)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

        if address == 0x10008734:
            return  # Run the real timer setter (base Receiver stubs it).
        if address == 0x10008CB0:
            ret((self.now * 1000) & 0x0FFFFFFF)
        elif address in (0x10008CA0, 0x10008CA8, 0x10008CB8, 0x1000FF98):
            ret(0)  # IRQ lock/unlock, hardware alarm and trace printf.
        elif address in (0x1000F72C, 0x1000F67E, 0x1000F650, 0x1000F7D4):
            r0, r1 = (cpu.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1))
            if address == 0x1000F72C:
                ret(bits(r0))
            elif address == 0x1000F67E:
                ret(bits(floating(r0) * floating(r1)))
            elif address == 0x1000F650:
                ret(bits(floating(r0) + floating(r1)))
            else:
                ret(int(floating(r0)))
        elif address == 0x10012BC4:
            self.draws.append(self.now)  # Execute the actual renderer below.
        elif address == 0x100132A8:
            self.flushes.append((self.now, bytes(cpu.mem_read(0x2000283C, 144))))
            ret()  # Physical LED hardware is not emulated.
        elif address == 0x10013AC4:
            ret()  # Physical driver's poll routine is not emulated.
        else:
            super().hook(cpu, address, size, user)

    def poll(self, now):
        self.now = now
        self.cpu.reg_write(UC_ARM_REG_SP, 0x20041000)
        self.cpu.reg_write(UC_ARM_REG_LR, self.stop | 1)
        self.cpu.emu_start(0x10013451, 0, count=50000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == self.stop, 'LED scheduler did not return'

    def command(self, now):
        self.now = now
        self.set_mode(struct.pack('<IHBBBB', 0x244792F5, 0, 255, 10, 0, 0))


def check(image):
    for leds in (33, 36):
        side = ScheduledReceiver(image, leds)
        side.update_palette(1, bytes([100, 160, 0, 0]))
        side.update(0, False, [1] * leds + [0] * (36 - leds))
        # Normal stream: one draw per MODE, delay clears after each actual draw.
        for now in range(500):
            if now % 20 == 0:
                side.command(now)
            side.poll(now)
        assert side.draws == list(range(10, 500, 20)), side.draws
        assert len(side.flushes) == len(side.draws)
        output = side.flushes[-1][1]
        assert any(output) and len(set(output[i:i+4] for i in range(0, leds*4, 4))) == 1
        assert output[leds*4:] == bytes((36-leds)*4)
        # Backlog delivered in a burst: every same-mode command resets deadline.
        side = ScheduledReceiver(image, leds)
        for now in range(200):
            side.command(now)
            side.poll(now)
        assert not side.draws, 'Redraw should be postponed by rapid MODE commands'
        for now in range(200, 1000):
            side.poll(now)
        assert side.draws == [209], side.draws
        # Drop just ONE mode command after a successful frame: subsequent modes
        # recover; a single dropped redraw cannot explain a sustained freeze.
        side.command(1000)
        for now in range(1000, 1100):
            if now == 1040:
                side.command(now)
            side.poll(now)
        assert side.draws[-2:] == [1010, 1050]
    print('Actual RP2040 scheduler/timers/Layer renderer: 33/36 LEDs draw every 20 ms; burst MODE traffic postpones redraw but recovers 10 ms after burst ends')
    print('Physical transport/driver remain outside this scheduler test; scheduler alone produces no permanent freeze')


if __name__ == '__main__':
    stock = (Path(__file__).resolve().parents[2] / 'build/vendor/keyscanner-v1.5.0.bin').read_bytes()
    check(patcher.patch(stock))
