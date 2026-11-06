#!/usr/bin/env python3
"""Send only the custom splash command; never flash or edit a keymap."""
import argparse
import glob
import os
import select
import sys
import termios
import time

parser = argparse.ArgumentParser()
parser.add_argument('action', choices=['on', 'off', 'status'])
parser.add_argument('--port', help='Keyboard serial port; otherwise find one Raise2 in /dev/serial/by-id')
args = parser.parse_args()
ports = [p for p in glob.glob('/dev/serial/by-id/*') if 'raise2' in p.lower()]
if not args.port and len(ports) != 1:
    parser.error('Specify --port: exactly one Raise2 serial port could not be identified.')
port = args.port or ports[0]
try:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
except OSError as error:
    parser.exit(1, f'Cannot open {port}: {error}. Close Bazecor before using this helper.\n')
previous = termios.tcgetattr(fd)

def request(command):
    os.write(fd, command)
    deadline = time.monotonic() + 2
    response = bytearray()
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], max(0, deadline - time.monotonic()))
        if ready:
            response.extend(os.read(fd, 4096))
            if any(line.strip() == b'.' for line in response.splitlines()):
                return [line.strip() for line in response.splitlines()
                        if line.strip() and line.strip() != b'.']
    raise RuntimeError('No complete response. Close Bazecor and check the serial port.')

try:
    attributes = termios.tcgetattr(fd)
    attributes[0] = attributes[1] = attributes[3] = 0
    attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attributes[4] = attributes[5] = termios.B9600
    attributes[6][termios.VMIN] = attributes[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attributes)
    termios.tcflush(fd, termios.TCIFLUSH)
    if request(b'splash.protocol\n') != [b'2']:
        raise RuntimeError('This helper requires v1.5.0-splash2 with patched side firmware; it will not enable the retired build.')
    if args.action in ('on', 'off'):
        request(b'splash2 1\n' if args.action == 'on' else b'splash2 0\n')
    response = request(b'splash2\n')
    if response not in ([b'0'], [b'1']):
        raise RuntimeError('Firmware did not report a splash state. Check that the custom firmware is installed.')
    enabled = response == [b'1']
    if args.action in ('on', 'off') and enabled != (args.action == 'on'):
        raise RuntimeError('The firmware did not retain the requested splash state.')
    print(f'Splash/Heatmap is {"enabled" if enabled else "disabled"}.')
    if enabled and args.action != 'status':
        print('The animation runs on Neuron; this helper does not need to stay running.')
except (OSError, RuntimeError) as error:
    print(f'Error: {error}', file=sys.stderr)
    sys.exit(1)
finally:
    termios.tcsetattr(fd, termios.TCSANOW, previous)
    os.close(fd)
