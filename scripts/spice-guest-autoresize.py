#!/usr/bin/env python3
"""X11 QXL guest helper. Run as the desktop user, never as root.
Install: apk add python3 py3-xlib xrandr (as root), then run this --install.
Applies connected Virtual-* preferred modes at login and on DRM hotplug / RandR events.
Intentionally owns their horizontal layout; do not save competing LXQt profiles.
"""
import argparse
import fcntl
import logging
import os
from pathlib import Path
import re
import select
import socket
import shutil
import subprocess
import time


def plan(text):
    outputs = []
    current = None
    for line in text.splitlines():
        header = re.match(r'^(Virtual-\d+) (connected|disconnected)(?: primary)?(?: (\d+x\d+)([+-]\d+)([+-]\d+))?', line)
        if header:
            name, state, size, x, y = header.groups()
            current = dict(name=name, connected=state == 'connected', size=size,
                           x=int(x) if x else None, y=int(y) if y else None, preferred=None)
            outputs.append(current)
        elif line and not line[0].isspace():
            current = None
        elif current:
            mode = re.match(r'^\s+(\d+x\d+)\s+(.*)', line)
            if mode and '+' in mode[2] and current['preferred'] is None:
                current['preferred'] = mode[1]
    if not any(o['connected'] and o['preferred'] for o in outputs):
        return []  # Never blank the desktop when mode information is incomplete.
    args = []
    changed = False
    x = 0
    for output in sorted(outputs, key=lambda o: int(o['name'].split('-')[-1])):
        if not output['connected']:
            if output['size']:
                args += ['--output', output['name'], '--off']
                changed = True
            continue
        mode = output['preferred']
        if mode is None:
            return []  # Wait for a complete topology, rather than move other heads over it.
        changed |= output['size'] != mode or output['x'] != x or output['y'] != 0
        args += ['--output', output['name'], '--mode', mode, '--pos', f'{x}x0', '--rotate', 'normal']
        x += int(mode.split('x')[0])
    return args if changed else []


def install():
    target = Path.home() / '.local/bin/spice-guest-autoresize.py'
    target.parent.mkdir(parents=True, exist_ok=True)
    if Path(__file__).resolve() != target.resolve():
        shutil.copyfile(__file__, target)
    target.chmod(0o755)
    directory = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'autostart'
    directory.mkdir(parents=True, exist_ok=True)
    # Desktop-entry Exec quoting differs from shell quoting.
    quoted = str(target).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
    (directory / 'spice-guest-autoresize.desktop').write_text(
        '[Desktop Entry]\nType=Application\nName=SPICE guest automatic resize\n'
        f'Exec=/usr/bin/python3 "{quoted}"\nTerminal=false\n')
    print(f'Installed {target}\nAutostart enabled. Log out and back in, or run {target} now.')


def is_drm_hotplug(packet, sender_pid=0):
    fields = packet.split(b'\0')
    return sender_pid == 0 and b'SUBSYSTEM=drm' in fields and b'HOTPLUG=1' in fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true')
    parser.add_argument('--once', action='store_true')
    options = parser.parse_args()
    if options.install:
        install()
        return
    state = Path(os.environ.get('XDG_STATE_HOME', str(Path.home() / '.local/state'))) / 'spice-guest-autoresize'
    state.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(filename=state / 'helper.log', level=logging.INFO, format='%(asctime)s %(message)s')
    logging.info('Starting helper v2 pid=%s DISPLAY=%s', os.getpid(), os.environ.get('DISPLAY'))
    from Xlib import display
    from Xlib.ext import randr
    with (state / 'helper.lock').open('w') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            logging.info("Another instance already holds the lock; exiting")
            return
        connection = display.Display()
        if not connection.has_extension('RANDR'):
            raise RuntimeError('This X server does not support RandR')
        connection.xrandr_query_version()
        root = connection.screen().root
        root.xrandr_select_input(randr.RRScreenChangeNotifyMask | randr.RRCrtcChangeNotifyMask |
                                randr.RROutputChangeNotifyMask | randr.RROutputPropertyNotifyMask)
        connection.sync()
        # QXL's mode hints also arrive as Linux DRM uevents; Xorg need not
        # emit a RandR change until a client queries the updated output modes.
        kernel = None
        try:
            kernel = socket.socket(socket.AF_NETLINK, socket.SOCK_DGRAM, 15)
            kernel.bind((0, 1))
            kernel.setblocking(False)
            logging.info('Listening for DRM hotplug and RandR events')
        except OSError:
            if kernel is not None:
                kernel.close()
            kernel = None
            logging.exception('DRM hotplug subscription unavailable; RandR only')
        last_failed = None

        def apply():
            nonlocal last_failed
            query = subprocess.run(['xrandr', '--query'], capture_output=True, text=True, timeout=10)
            if query.returncode:
                logging.error('query: %s', query.stderr.strip())
                return
            args = plan(query.stdout)
            signature = (query.stdout, tuple(args))
            if not args or signature == last_failed:
                return
            result = subprocess.run(['xrandr', *args], capture_output=True, text=True, timeout=10)
            if result.returncode:
                last_failed = signature
                logging.error('apply %s: %s', args, result.stderr.strip())
            else:
                last_failed = None
                logging.info('Applied %s', args)

        apply()
        if options.once:
            return
        deadline = None
        while True:
            if connection.pending_events():
                while connection.pending_events():
                    connection.next_event()
                deadline = time.monotonic() + 0.5
            timeout = None if deadline is None else max(0, deadline - time.monotonic())
            readers = [connection.fileno()] + ([kernel] if kernel is not None else [])
            readable, _, _ = select.select(readers, [], [], timeout)
            if kernel is not None and kernel in readable:
                hotplug = False
                while True:
                    try:
                        packet, sender = kernel.recvfrom(65536)
                    except BlockingIOError:
                        break
                    hotplug |= is_drm_hotplug(packet, sender[0])
                if hotplug:
                    logging.info('DRM hotplug received; refreshing preferred modes')
                    deadline = time.monotonic() + 0.5
            if connection.fileno() in readable:
                connection.next_event()
                deadline = time.monotonic() + 0.5
            if not readable and deadline is not None:
                deadline = None
                apply()

if __name__ == '__main__':
    try:
        main()
    except Exception:
        logging.exception('Helper stopped unexpectedly')
        raise
