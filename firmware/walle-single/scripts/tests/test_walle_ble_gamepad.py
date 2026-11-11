import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class BleGamepadTests(unittest.TestCase):
    def test_hid_report_decoding(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = str(pathlib.Path(directory) / 'hid-test')
            subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                            '-I' + str(ROOT/'main/boards/walle-single'),
                            str(ROOT/'scripts/tests/walle_ble_gamepad_test.cc'),
                            '-o', binary], check=True)
            subprocess.run([binary], check=True, timeout=10)
