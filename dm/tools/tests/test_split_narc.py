"""shader_split: Camelot's Nitro archives (Mario Tennis Aces) are opened and each member unpacked by its first byte (narc_members, camelot_member, camelot_lz). No game files needed."""
import os
import struct
import sys
import unittest
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / 'dm/gfx/tools'))
_had = os.environ.get('DM_GAME'); os.environ['DM_GAME'] = '0100199023B2E000'   # shader_split loads a profile at import; other tests expect no default game
import shader_split as ss
if _had is None: del os.environ['DM_GAME']
else: os.environ['DM_GAME'] = _had


def narc(members):
    fat = b''.join(struct.pack('<II', sum(map(len, members[:i])), sum(map(len, members[:i + 1]))) for i in range(len(members)))
    btaf = b'BTAF' + struct.pack('<IHH', 12 + len(fat), len(members), 0) + fat
    btnf = b'BTNF' + struct.pack('<I', 16) + bytes(8)
    data = b''.join(members)
    gmif = b'GMIF' + struct.pack('<I', 8 + len(data)) + data
    body = btaf + btnf + gmif
    return b'NARC\xfe\xff\x00\x01' + struct.pack('<IHH', 16 + len(body), 16, 3) + body


def lz(payload, n): return b'\x50\0\0\0' + n.to_bytes(4, 'big') + payload


class Narc(unittest.TestCase):
    def test_lz_literals_and_matches(self):
        # V F X B ' ' then a match 1 back of 3 ('   '), then 'a' 'b'; the next group starts with a match of distance 0, the end
        self.assertEqual(ss.camelot_lz(lz(b'\xfcVFXB \x13\x00ab\x80\x00\x00', 10), 8, 10, 't'), b'VFXB    ab')

    def test_lz_long_lengths_and_zero_flags(self):
        # a flag byte of 0: eight literals; then length 0 (16 + 4 = 20 bytes, 8 back) and length 1 (17 + 2 + 0xff = 0x112 bytes, 4 back)
        src = b'\x00abcdefgh' + b'\x20' + b'\x80\x00\x04' + b'\x41\x00\x02\x00' + b'\x00\x00'
        out = b'abcdefgh' + (b'abcdefgh' * 3)[:20]
        out += bytes(out[len(out) - 4 + i % 4] for i in range(0x112))
        self.assertEqual(ss.camelot_lz(lz(src, len(out)), 8, len(out), 't'), out)

    def test_members_by_first_byte(self):
        plain = b'BNSH' + bytes(60)
        z = zlib.compressobj(9, zlib.DEFLATED, -15); raw = z.compress(plain) + z.flush()
        m30 = b'\x30' + len(plain).to_bytes(4, 'big') + bytes(11) + raw
        m20 = b'\x20' + len(plain).to_bytes(4, 'big') + bytes(11) + zlib.compress(plain)
        m80 = b'\x80' + len(plain).to_bytes(4, 'big') + bytes(11) + plain
        stored = b'FSHA' + bytes(20)
        got = [m for _, m in ss.narc_members(narc([m30, m20, m80, stored, lz(b'\xfcVFXB \x13\x00ab\x80\x00\x00', 10)]), 't')]
        self.assertEqual(got, [plain, plain, plain, stored, b'VFXB    ab'])

    def test_bad_archives_are_refused(self):
        d = bytearray(narc([b'x' * 32]))
        struct.pack_into('<I', d, 16 + 16, 0x100)   # the member's end past the data
        with self.assertRaises(SystemExit): list(ss.narc_members(bytes(d), 't'))
        with self.assertRaises(SystemExit): list(ss.narc_members(narc([lz(b'\x80\x12\x00', 4)]), 't'))   # a match before any output
        with self.assertRaises(SystemExit): list(ss.narc_members(narc([b'\x20' + (1 << 20).to_bytes(4, 'big') + bytes(11)]), 't'))   # expands more than 256 times


if __name__ == '__main__':
    unittest.main()
