# SPDX-License-Identifier: LGPL-3.0-or-later
import importlib.util
import sys

sys.dont_write_bytecode = True
import unittest
from pathlib import Path
from unittest import mock
import tempfile
import types
spec = importlib.util.spec_from_file_location('helper', Path(__file__).resolve().parents[1] / 'scripts/spice-guest-autoresize.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
class PlanTest(unittest.TestCase):
    def test_installer_creates_user_autostart_and_copies_script(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory) / 'desktop user'
            config = Path(directory) / 'config'
            with mock.patch.object(helper.Path, 'home', return_value=home), mock.patch.dict(helper.os.environ, {'XDG_CONFIG_HOME':str(config)}), mock.patch('builtins.print'):
                helper.install()
                helper.install()  # Updating an existing installation is supported.
            installed = home / '.local/bin/spice-guest-autoresize.py'
            self.assertEqual(installed.read_bytes(), Path(helper.__file__).read_bytes())
            self.assertTrue(installed.stat().st_mode & 0o100)
            entry = (config / 'autostart/spice-guest-autoresize.desktop').read_text()
            self.assertIn(f'Exec=/usr/bin/python3 "{installed}"', entry)
            self.assertIn('Type=Application', entry)
    def test_kernel_only_event_refreshes_modes_without_randr_event(self):
        kernel = mock.Mock()
        kernel.recvfrom.side_effect = [(b'SUBSYSTEM=drm\0HOTPLUG=1\0', (0,1)), BlockingIOError()]
        connection = mock.Mock()
        connection.has_extension.return_value = True
        connection.pending_events.return_value = 0
        connection.fileno.return_value = 77
        xlib = types.ModuleType('Xlib')
        xlib.display = types.SimpleNamespace(Display=lambda: connection)
        ext = types.ModuleType('Xlib.ext')
        ext.randr = types.SimpleNamespace(RRScreenChangeNotifyMask=1, RRCrtcChangeNotifyMask=2, RROutputChangeNotifyMask=4, RROutputPropertyNotifyMask=8)
        initial = types.SimpleNamespace(returncode=0, stdout='Virtual-1 connected 800x600+0+0\n   800x600 60*+\n', stderr='')
        changed = types.SimpleNamespace(returncode=0, stdout='Virtual-1 connected 800x600+0+0\n   1024x768 60+\n', stderr='')
        success = types.SimpleNamespace(returncode=0, stdout='', stderr='')
        with tempfile.TemporaryDirectory() as state, mock.patch.dict('sys.modules', {'Xlib':xlib, 'Xlib.ext':ext}), mock.patch.dict(helper.os.environ, {'XDG_STATE_HOME':state}), mock.patch('sys.argv', ['helper']), mock.patch.object(helper.socket, 'socket', return_value=kernel), mock.patch.object(helper.select, 'select', side_effect=[([kernel],[],[]), ([],[],[]), StopIteration]), mock.patch.object(helper.subprocess, 'run', side_effect=[initial, changed, success]) as run, mock.patch.object(helper.logging, 'basicConfig'):
            with self.assertRaises(StopIteration): helper.main()
            self.assertIn('1024x768', run.call_args_list[-1].args[0])
            self.assertEqual(run.call_count,3)
            connection.next_event.assert_not_called()
    def test_drm_hotplug_filter(self):
        self.assertTrue(helper.is_drm_hotplug(b'change@/devices/drm/card0\0SUBSYSTEM=drm\0HOTPLUG=1\0'))
        self.assertFalse(helper.is_drm_hotplug(b'SUBSYSTEM=input\0HOTPLUG=1\0'))
        self.assertFalse(helper.is_drm_hotplug(b'SUBSYSTEM=drm\0'))
        self.assertFalse(helper.is_drm_hotplug(b'SUBSYSTEM=drm\0HOTPLUG=1\0', 123))
    def test_preferred_differs_from_active(self):
        args = helper.plan('Virtual-1 connected 1920x953+0+0\n   1920x953 59.98*+\nVirtual-2 connected primary 1024x687+1920+0\n   1920x953 59.98 +\n')
        self.assertEqual(args[args.index('Virtual-2')+1:][:4], ['--mode','1920x953','--pos','1920x0'])
    def test_already_applied(self):
        self.assertEqual(helper.plan('Virtual-1 connected 800x600+0+0\n   800x600 60*+\nVirtual-2 connected 1024x768+800+0\n   1024x768 60*+\n'), [])
    def test_connected_disabled_at_login(self):
        args = helper.plan('Virtual-1 connected\n   800x600 60 +\nVirtual-2 connected\n   1024x768 60 +\n')
        self.assertIn('1024x768', args)
        self.assertIn('800x0', args)
    def test_incomplete_keeps_desktop(self):
        self.assertEqual(helper.plan('Virtual-1 connected 800x600+0+0\n   800x600 60*+\nVirtual-2 connected\n'), [])
    def test_nonvirtual_outputs_ignored(self):
        self.assertEqual(helper.plan('HDMI-1 connected 800x600+0+0\n   800x600 60*+\n'), [])
    def test_disconnected_active_output_disabled(self):
        args = helper.plan('Virtual-1 connected 800x600+0+0\n   800x600 60*+\nVirtual-2 disconnected 800x600+800+0\n')
        self.assertIn('--off',args)
if __name__ == '__main__':
    unittest.main()
