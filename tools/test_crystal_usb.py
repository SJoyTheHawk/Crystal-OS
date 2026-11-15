#!/usr/bin/env python3
"""Exercise host USB framing and diagnostics without opening a board port."""
from contextlib import ExitStack
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import crystal


class UsbInstallTest(unittest.TestCase):
    def setUp(self):
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        directory = self.stack.enter_context(tempfile.TemporaryDirectory())
        self.package = Path(directory) / 'test.capp'
        self.payload = b'x' * 2051
        self.package.write_bytes(self.payload)
        self.stack.enter_context(patch.object(crystal.os, 'open', return_value=42))
        self.close = self.stack.enter_context(patch.object(crystal.os, 'close'))
        self.write = self.stack.enter_context(patch.object(
            crystal.os, 'write', side_effect=lambda fd, data: len(data)))
        self.read = self.stack.enter_context(patch.object(crystal.os, 'read'))
        self.stack.enter_context(patch.object(
            crystal.select, 'select', return_value=([42], [], [])))
        self.stack.enter_context(patch('termios.tcgetattr', return_value=[0] * 6 + [[0] * 32]))
        self.stack.enter_context(patch('termios.tcsetattr'))

    def install(self):
        return crystal.install_usb('/dev/test-usb', self.package)

    def test_success_with_logs_and_chunk_acknowledgements(self):
        self.read.side_effect = [
            b'boot log\nCRYSTAL/1 RE',
            b'ADY\r\nCRYSTAL/1 ACK 2048\nCRYSTAL/1 ACK 2051\n'
            b'CRYSTAL/1 OK {"installed":true,"restart_required":true}\n',
        ]
        self.assertEqual(self.install(), {'installed': True, 'restart_required': True})
        self.assertEqual([call.args[1] for call in self.write.call_args_list], [
            b'CRYSTAL/1 INSTALL 2051\n', self.payload[:2048], self.payload[2048:],
        ])
        self.close.assert_called_once_with(42)

    def test_device_error_before_ready_is_preserved(self):
        self.read.return_value = b'CRYSTAL/1 ERROR install busy\n'
        with self.assertRaisesRegex(ValueError, 'device USB install failed: install busy'):
            self.install()
        self.write.assert_called_once_with(42, b'CRYSTAL/1 INSTALL 2051\n')
        self.close.assert_called_once_with(42)

    def test_unexpected_handshake_is_reported(self):
        self.read.return_value = b'CRYSTAL/1 ACK 100\n'
        with self.assertRaisesRegex(ValueError, 'unexpected USB upload response: CRYSTAL/1 ACK 100'):
            self.install()
        self.assertEqual(self.write.call_count, 1)

    def test_silent_endpoint_explains_transport_mismatch(self):
        with patch.object(crystal.time, 'monotonic', side_effect=[0, 31]):
            with self.assertRaises(ValueError) as raised:
                self.install()
        message = str(raised.exception)
        for detail in ('/dev/test-usb', 'serial monitor', 'USB Serial/JTAG', 'CH343', '--wifi'):
            self.assertIn(detail, message)
        self.assertEqual(self.write.call_count, 1)
        self.close.assert_called_once_with(42)

    def test_progress_error_is_preserved(self):
        self.read.return_value = b'CRYSTAL/1 READY\nCRYSTAL/1 ERROR USB package upload failed\n'
        with self.assertRaisesRegex(ValueError, 'device USB install failed: USB package upload failed'):
            self.install()
        self.assertEqual(self.write.call_count, 2)


if __name__ == '__main__':
    unittest.main()
