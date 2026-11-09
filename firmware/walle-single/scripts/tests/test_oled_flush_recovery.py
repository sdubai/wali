"""Exercise the installed LVGL wait loop with a lost I2C completion."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class OledFlushRecoveryTest(unittest.TestCase):
    def test_missing_completion(self):
        source = ROOT / 'managed_components/lvgl__lvgl/src/core/lv_refr.c'
        if not source.exists():
            self.skipTest('Build dependencies must be installed first')
        text = source.read_text()
        start = text.index('static void wait_for_flushing(lv_display_t * disp)\n{')
        opening = text.index('{', start)
        depth = 1
        end = opening + 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        function = text[start:end]
        with tempfile.TemporaryDirectory() as tmp:
            directory = pathlib.Path(tmp)
            (directory / 'lvgl.h').write_text('''#pragma once
struct lv_display_t { volatile int flushing; int flushing_last; void (*flush_wait_cb)(lv_display_t*); int ready_calls; };
inline void lv_display_flush_ready(lv_display_t* d) { d->flushing=0; ++d->ready_calls; }
#define LV_PROFILER_REFR_BEGIN
#define LV_PROFILER_REFR_END
#define LV_LOG_TRACE(...)
#define LV_EVENT_FLUSH_WAIT_START 0
#define LV_EVENT_FLUSH_WAIT_FINISH 1
inline void lv_display_send_event(lv_display_t*, int, void*) {}
''')
            (directory / 'test.cc').write_text('''#include "oled_flush_recovery.h"
#include <cassert>
#include <cstddef>
''' + function + '''
int main(int argc, char**) {
 lv_display_t d{1,1,nullptr,0};
 if(argc>1) { wait_for_flushing(&d); return 1; }
 d.flush_wait_cb = walle::CompleteSynchronousOledFlush;
 for(int i=0;i<10000;++i) {
   d.flushing=1; d.flushing_last=1;
   wait_for_flushing(&d);
   assert(!d.flushing && !d.flushing_last && d.ready_calls==i+1);
 }
 wait_for_flushing(&d);
 assert(d.ready_calls==10000);
}
''')
            binary = directory / 'test'
            subprocess.run(['clang++', '-std=c++17', '-I'+tmp,
                            '-I'+str(ROOT / 'main/display'),
                            str(directory / 'test.cc'), '-o', str(binary)], check=True)
            with self.assertRaises(subprocess.TimeoutExpired):
                subprocess.run([str(binary), 'legacy'], timeout=0.3, check=True)
            subprocess.run([str(binary)], timeout=2, check=True)


if __name__ == '__main__':
    unittest.main()
