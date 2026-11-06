#!/usr/bin/env python3
"""Execute released side radio-disconnect -> wired-notification instructions.

No hardware access. The real event handler, disconnect routine, callback,
Communications sender, wired sender and CRC8 execute in Unicorn. Only hardware
radio stop, scheduler yield, libc memcpy, display effect and queue insertion
are substituted. The callback table has the layout/entry installed by the real
WiredCommunication initializer at 0x1001B65E..0x1001B710.
"""
from pathlib import Path
import struct
import sys
from side_binary import Receiver, patcher
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP


class RadioDisconnect(Receiver):
    wired, radio, wired_link, ble_mode = 0x2000B723, 0x2000B71C, 0x2000B720, 0x2000B721
    radio_peer, side_id = 0x2000B71F, 0x2000B71E

    def __init__(self, image, side, wired=True, ble=False, peer=True, radio=True):
        self.sent, self.notices = [], []
        super().__init__(image)
        for address, value in ((self.wired, wired), (self.radio, radio),
                               (self.wired_link, True), (self.ble_mode, ble),
                               (self.radio_peer, peer), (self.side_id, side+1)):
            self.cpu.mem_write(address, bytes([value]))
        # Actual device table contains [wired-left, BLE-left, wired-right, BLE-right].
        self.cpu.mem_write(0x20002E80, struct.pack('<I', 0x10035AA4 + side*2))
        self.cpu.mem_write(0x20002E78, struct.pack('<I', 0x20032000))
        # Real BindingCallbacks/Callback/std::function layout: DISCONNECTED(3)
        # invokes the released wired forwarder, manager at +8, invoker at +12.
        self.cpu.mem_write(0x20001E60, struct.pack('<3I', 0x20033000, 0x20033008, 0x20033008))
        self.cpu.mem_write(0x20033000, struct.pack('<2I', 3, 0x20033100))
        self.cpu.mem_write(0x20033100, struct.pack('<3I', 0x20033200, 0x20033210, 0x20033210))
        self.cpu.mem_write(0x20033200, struct.pack('<4I', 0, 0, 0x1001AE91, 0x1001AE9D))

    def hook(self, cpu, address, size, user):
        if address == 0x1001AE9C:
            self.notices.append(bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1), 32)))
            return  # Execute the actual callback.
        if address == 0x1000678C:
            count = cpu.reg_read(UC_ARM_REG_R2)
            assert count == 32
            self.sent.append(bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1), count)))
            cpu.reg_write(UC_ARM_REG_R0, 0)  # Successful buffer_add, no physical wire.
        elif address not in (0x10008490, 0x1000A774, 0x1001290C):
            return super().hook(cpu, address, size, user)
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def event(self, event):
        self.cpu.reg_write(UC_ARM_REG_SP, 0x20041000)
        self.cpu.reg_write(UC_ARM_REG_R0, event)
        self.cpu.reg_write(UC_ARM_REG_R1, 0)
        self.cpu.reg_write(UC_ARM_REG_LR, self.stop | 1)
        self.cpu.emu_start(0x1001A4E9, 0, count=40000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == self.stop


def crc8(data):
    value = 0
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = ((value << 1) ^ (7 if value & 128 else 0)) & 255
    return value


def check(stock):
    assert len(stock) == 216832
    results = []
    for image in (stock, patcher.patch(stock)):
        packets = []
        for side in (0, 1):
            for ble in (False, True):
                receiver = RadioDisconnect(image, side, ble=ble)
                # Released radio event table at 0x100358A8 maps 0x0F to the
                # disconnect branch, which calls 0x1001A418 with notify=true.
                receiver.event(0x0F)
                assert receiver.cpu.mem_read(receiver.radio, 1) == b'\0'
                assert receiver.cpu.mem_read(receiver.wired, 1) == b'\1'
                assert receiver.cpu.mem_read(receiver.wired_link, 1) == b'\1'
                assert len(receiver.notices) == len(receiver.sent) == 1
                assert receiver.notices[0][:4] == bytes([3, 9+side, 0, 0])
                packet = receiver.sent[0]
                assert packet[:3] == bytes([3, 9+side if ble else 1+side, 0])
                assert packet[3] == crc8(packet[:3] + b'\0')
                assert packet[4:] == bytes(28)
                if not ble:
                    packets.append(packet)
            # A peer can drop before its upper-level radio handshake completes.
            receiver = RadioDisconnect(image, side, radio=False)
            receiver.event(0x0F)
            assert receiver.sent == [packets[-1]]
            for kwargs, event in (({'wired': False}, 0x0F), ({'peer': False}, 0x0F), ({}, 0x0E)):
                receiver = RadioDisconnect(image, side, **kwargs)
                receiver.event(event)
                assert not receiver.sent
        results.append(b''.join(packets))
    assert results[0] == results[1], 'The map patch changed disconnect behavior'
    print('Actual released side radio-disconnect event: wired link stays connected; DISCONNECTED is relabeled as wired-left/right and emitted with valid CRC8')
    print('Stock and patched side binaries emit identical disconnect packets; no map-patch instruction is involved')
    return results[0]


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[2]
    packets = check((root / 'build/vendor/keyscanner-v1.5.0.bin').read_bytes())
    if len(sys.argv) > 1:
        Path(sys.argv[1]).write_bytes(packets)
