"""python -I test_linkframe.py  (run from pc/). Cross-checks against the C codec."""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import linkframe as lf  # noqa: E402

assert lf.crc16(b"123456789") == 0x29B1
f = lf.encode(lf.T_TEXT, 7, "集合".encode())
d = lf.Decoder()
assert d.feed(b"\x00\xA5\x13" + f[:5]) == []
assert d.feed(f[5:]) == [(lf.T_TEXT, 7, "集合".encode())]
bad = bytearray(f); bad[8] ^= 1
d2 = lf.Decoder(); assert d2.feed(bytes(bad) + f) == [(lf.T_TEXT, 7, "集合".encode())] and d2.crc_errors == 1
assert lf.utf8_truncate("集合", 5) == "集".encode()
assert len(lf.utf8_truncate("滑" * 100)) == 180

# C codec produces identical bytes
here = os.path.dirname(os.path.abspath(__file__))
proto = os.path.join(here, "..", "protocol")
src = r'''
#include <stdio.h>
#include "link_frame.h"
int main(void){ uint8_t o[64]; size_t n = link_encode(1, 7, (const uint8_t*)"\xE9\x9B\x86\xE5\x90\x88", 6, o, sizeof o);
for (size_t i=0;i<n;i++) printf("%02x", o[i]); return 0; }'''
with tempfile.TemporaryDirectory() as t:
    c = os.path.join(t, "x.c"); exe = os.path.join(t, "x")
    open(c, "w").write(src)
    subprocess.check_call(["cc", "-std=c99", "-I", proto, c, os.path.join(proto, "link_frame.c"), "-o", exe])
    assert subprocess.check_output([exe]).decode() == f.hex(), "C/Python mismatch"
print("all linkframe.py tests passed (C and Python frames identical)")
