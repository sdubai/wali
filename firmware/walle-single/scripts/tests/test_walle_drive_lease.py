import pathlib
import shutil
import subprocess
import tempfile
import unittest


class WalleDriveLeaseTest(unittest.TestCase):
    def test_duration_watchdog_and_cancellation(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("C++ compiler required")
        board = pathlib.Path(__file__).resolve().parents[2] / "main/boards/lckfb/szpi-esp32s3"
        source = r'''
#include "drive_lease.h"
#include <cassert>
int main() {
    walle::DriveLease drive;
    assert(!drive.Active());
    assert(drive.Renew(0) == 0);
    constexpr int64_t start = 1000000;
    // Simulate the 1500ms motion-board watchdog across long and short requests.
    for (int duration : {100, 500, walle::kDefaultVoiceDriveMs, 20000, 60000}) {
        drive.Start(start, duration);
        int64_t motor_deadline = start;
        for (int64_t now = start; now < start + duration * 1000LL; now += 20000) {
            if (drive.RefreshDue(now)) {
                int lease = drive.Renew(now);
                assert(lease > 0 && lease <= 750);
                motor_deadline = now + lease * 1000LL;
                assert(!drive.RefreshDue(now));
            }
            assert(motor_deadline > now);
            assert(motor_deadline <= start + duration * 1000LL);
        }
        assert(drive.Expired(start + duration * 1000LL));
        assert(drive.Renew(start + duration * 1000LL) == 0);
    }
    // Loss of renewals cannot leave a 60s command latched in the motor board.
    drive.Start(start, 60000);
    assert(drive.Renew(start) == 750);
    drive.Cancel();
    assert(!drive.Active() && !drive.RefreshDue(start + 1000000));
    // Replacing an older move starts a fresh, shorter deadline.
    drive.Start(start, 60000);
    drive.Start(start + 1000000, 500);
    assert(drive.Expired(start + 1500000));
    // Defensive bounds, including an oversized duration and sub-minimum request.
    drive.Start(start, 1000000);
    assert(drive.Expired(start + 60000000));
    drive.Start(start, -1);
    assert(drive.Renew(start) == 100);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = pathlib.Path(directory) / "test.cc"
            binary = pathlib.Path(directory) / "test"
            cpp.write_text(source)
            subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(board), str(cpp), "-o", str(binary)],
                check=True, capture_output=True, text=True,
            )
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
