#!/usr/bin/env python3
"""Execute released final-output and deghost-init code; no physical hardware.

Uses an identity key-to-driver map and mocked SPI/GPIO boundaries. It cannot
simulate analog ghosting, confirm PCB mappings or establish a physical fix.
"""
from pathlib import Path
import json
import runpy
import struct
from side_binary import Receiver, root, patcher, check as check_maps
from side_scheduler import ScheduledReceiver, bits
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP


class OutputReceiver(ScheduledReceiver):
    def __init__(self, image):
        super().__init__(image, 33)
        self.gpio, self.frames = [], []
        # The driver uses a count, then a sentinel iteration to commit. Supply
        # an identity fixture map and all 44 RGBW slots in its 198-byte packet.
        self.cpu.mem_write(0x200029D8 + 12, bytes([33]))
        for address in (0x200029C4, 0x200029C8):
            self.cpu.mem_write(address, struct.pack('<I', bits(1)))
        self.cpu.mem_write(0x2000B3DC + 8, struct.pack('<I', 0x20032000) + bytes([33]))
        self.cpu.mem_write(0x20032000, bytes(range(33)))
        self.cpu.mem_write(0x200027E8, struct.pack('<III', 0x20033000, 0x20033000+44*4, 0x20033000+44*4))

    def hook(self, cpu, address, size, user):
        def ret(value=None):
            if value is not None:
                cpu.reg_write(UC_ARM_REG_R0, value)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        if address in (0x100132A8, 0x10013AC4):
            return  # Execute actual output flush, driver mapping and packing.
        if address == 0x1000F6DC:
            ret(int(cpu.reg_read(UC_ARM_REG_R0) == cpu.reg_read(UC_ARM_REG_R1)))
        elif address == 0x10009FEC:
            self.gpio.append((self.now, cpu.reg_read(UC_ARM_REG_R0)))
            ret()  # Actual driver-control GPIO is not toggled.
        elif address == 0x10008C48:
            ret()  # Actual underglow GPIO is not toggled.
        elif address == 0x10009FA8:
            self.frames.append(bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R0), 198)))
            ret(0)  # Capture packed SPI output instead of using hardware.
        elif address == 0x1000A000:
            ret()  # SPI transaction state machine is tested separately below.
        elif address == 0x1000FA2C:
            dst, src, count = [cpu.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2)]
            assert count <= 512
            cpu.mem_write(dst, bytes(cpu.mem_read(src, count)))
            ret(dst)
        else:
            super().hook(cpu, address, size, user)


class RegisterReceiver(Receiver):
    def __init__(self, image):
        super().__init__(image)
        self.writes = []
        self.cpu.mem_write(0x20002F80, struct.pack('<I', 0x20034000))
        self.cpu.mem_write(0x20034000, bytes([4]))  # Actual deghost-initialization state.
        self.cpu.mem_write(0x20034004, struct.pack('<I', 0x20035000))

    def hook(self, cpu, address, size, user):
        if address in (0x10008274, 0x100082B8, 0x10008490, 0x10008324):
            if address == 0x10008324:
                self.writes.append(bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1), cpu.reg_read(UC_ARM_REG_R2))))
            cpu.reg_write(UC_ARM_REG_R0, 0)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        else:
            super().hook(cpu, address, size, user)

    def run(self):
        self.cpu.reg_write(UC_ARM_REG_SP, 0x20041000)
        self.cpu.reg_write(UC_ARM_REG_LR, self.stop | 1)
        self.cpu.emu_start(0x1000A001, 0, count=3000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == self.stop
        assert self.cpu.mem_read(0x20034000, 1)[0] == 5
        return self.writes


def check_output(image):
    side = OutputReceiver(image)
    side.update(0, False, [1]*33+[0]*3)
    for n, color in enumerate(([80,0,0,0], [0,28,0,0], [0,0,0,0])):
        old = len(side.frames)
        side.update_palette(1, bytes(color))
        side.command(n*40)
        side.poll(n*40+10)
        if n == 1:
            assert side.frames[-1][:4] == bytes([0,1,0,0])
            for led in range(33):
                offset = led*4 + (led//4)*2
                assert side.frames[-1][offset:offset+4] == bytes([0,1,0,0])
        if n == 2:
            assert side.cpu.mem_read(0x2000283C, 144) == bytes(144)
            assert len(side.frames) == old, 'Black unexpectedly queued a PWM frame'
            assert side.gpio[-1][1] == 0, 'Black did not lower driver control'
            assert side.frames[-1][:4] == bytes([0,1,0,0])
    print('Actual side output path: final green sends R=0/G=1; black lowers driver control without queuing zero PWM')


if __name__ == '__main__':
    stock = (root/'build/vendor/keyscanner-v1.5.0.bin').read_bytes()
    normal = patcher.patch(stock)
    experiment = runpy.run_path(str(root/'side-patches/tools/patch-side-deghost.py'))
    experimental = experiment['patch'](stock)
    metadata = json.loads((root/'side-patches/patches/raise2-v1.5.0-deghost-experiment.json').read_text())
    assert RegisterReceiver(normal).run() == [bytes.fromhex('5202b3')]
    assert RegisterReceiver(experimental).run() == [bytes.fromhex('5202b1')]
    print('Actual released initialization: ordinary SPI 52 02 B3; experiment SPI 52 02 B1; CS pull-up selection only')
    check_output(normal)
    check_output(experimental)
    check_maps(stock, experimental)
    for wrong in (stock[:-1], stock+b'\0', normal, experimental):
        try:
            experiment['patch'](wrong)
        except ValueError:
            pass
        else:
            raise AssertionError('Experimental patch accepted an unexpected input')
    import hashlib
    assert hashlib.sha256(experimental).hexdigest() == metadata['output_sha256']
    print('Deghost experiment: exact image/hash/CRC/boundaries, map regression and rejected inputs passed; physical red-tail fix unconfirmed')
