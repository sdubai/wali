import pathlib, re, json, unittest, importlib.util
ROOT=pathlib.Path(__file__).resolve().parents[2]
BOARD=ROOT/'main/boards/walle-single'
class WalleSingleTests(unittest.TestCase):
    def test_pin_allocation_does_not_overlap_motion_or_usb(self):
        audio=(BOARD/'config.h').read_text()
        audio_pins={int(x) for x in re.findall(r'#define AUDIO_\w+ GPIO_NUM_(\d+)',audio)}
        buttons={int(x) for x in re.findall(r'Button key\d+_\{GPIO_NUM_(\d+)',(BOARD/'single_board.cc').read_text())}
        oled={int(x) for x in re.findall(r"#define WALLE_OLED_(?:SDA|SCL) GPIO_NUM_(\d+)",audio)}
        motor={4,5,6,7,15,16,10,11,1,2,14,21,8,9,19,20}
        self.assertEqual(len(oled),2)
        self.assertFalse(oled & (audio_pins | buttons | motor))
        self.assertEqual(audio_pins,{12,13,17,18})
        self.assertEqual(buttons,{38,39,40,41})
        self.assertFalse(audio_pins & buttons or audio_pins & motor or buttons & motor)
    def test_single_board_identity_and_button_only_voice(self):
        cfg=json.loads((BOARD/'config.json').read_text())
        self.assertEqual(cfg['type'],'walle-single')
        opts=cfg['builds'][0]['sdkconfig_append']
        self.assertIn('CONFIG_WAKE_WORD_DISABLED=y',opts)
        self.assertIn('CONFIG_USE_DEVICE_AEC=n',opts)
        self.assertIn('CONFIG_BOARD_TYPE_WALLE_SINGLE=y',opts)
        self.assertEqual((BOARD/'single_board.cc').read_text().count('DECLARE_BOARD('),1)
    def test_motion_watchdog_and_calibration_are_retained(self):
        s=(BOARD/'board_config.h').read_text()
        self.assertRegex(s,r'#define WALLE_DRIVE_TIMEOUT_MS\s+1500')
        self.assertRegex(s,r'#define WALLE_INDEPENDENT_SERVO_CALIBRATION\s+1')
    def test_local_drive_lease_matches_tested_original(self):
        old=ROOT/'main/boards/lckfb/szpi-esp32s3/drive_lease.h'
        self.assertEqual(old.read_bytes(),(BOARD/'drive_lease.h').read_bytes())
