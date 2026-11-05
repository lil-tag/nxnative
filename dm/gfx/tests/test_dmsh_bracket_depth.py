#!/usr/bin/env python3
"""Blocks nested deeper than clang's default bracket depth (256) compile with the chain's Metal commands (dmsh_build.py MAC_METAL, ios_metal.py METAL: -fbracket-depth=1024).
The translator nests a block for every forward branch to a common exit, and Ocarina of Time's agl_technique_pfx compute shaders 1f78730053feb036 and e172bd7ec218ab8e, an unrolled
256-element walk with an early exit after each step, reach 257: `fatal error: bracket nesting level exceeded maximum of 256`. Compiling needs xcrun (skipped without it)."""
import shutil, subprocess, sys, tempfile, unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / 'dm/ios/tools')); sys.path.insert(0, str(REPO / 'dm/gfx/tools'))
import ios_metal   # noqa: E402

DEPTH = 300


def nested_kernel(depth):   # what the translator writes for such a walk: each step's test opens a block the rest of the walk lies in
    body = ''.join(f'{"    " * (i + 1)}uint t{i} = min(left, a[{i}]); a[{i}] -= t{i}; left -= t{i};\n{"    " * (i + 1)}if (left != 0)\n{"    " * (i + 1)}{{\n' for i in range(depth))
    close = ''.join(f'{"    " * (i + 1)}}}\n' for i in reversed(range(depth)))
    return f'#include <metal_stdlib>\nusing namespace metal;\nkernel void k(device uint* a [[buffer(0)]], uint id [[thread_position_in_grid]])\n{{\n    uint left = id;\n{body}{close}}}\n'


def mac_metal():   # dmsh_build.py's list, read from its source: importing it runs the build
    src = (REPO / 'dm/gfx/tools/dmsh_build.py').read_text()
    line = next(l for l in src.splitlines() if l.startswith('MAC_METAL = '))
    return eval(line.split('=', 1)[1])


class BracketDepth(unittest.TestCase):
    def test_both_commands_raise_it(self):
        for cmd in (mac_metal(), ios_metal.METAL):
            self.assertIn('-fbracket-depth=1024', cmd)
        self.assertIn('-fbracket-depth=1024', ios_metal.RECORD)

    @unittest.skipUnless(shutil.which('xcrun'), 'needs xcrun')
    def test_a_walk_nested_past_256_compiles(self):
        with tempfile.TemporaryDirectory(prefix='bracket_') as d:
            msl = Path(d) / 'k.metal'; msl.write_text(nested_kernel(DEPTH))
            for cmd in (mac_metal(), ios_metal.METAL):
                r = subprocess.run([*cmd, str(msl), '-o', str(Path(d) / 'k.air')], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stderr[-2000:])
            plain = [a for a in mac_metal() if not a.startswith('-fbracket-depth')]
            r = subprocess.run([*plain, str(msl), '-o', str(Path(d) / 'p.air')], capture_output=True, text=True)
            self.assertIn('bracket nesting level exceeded', r.stderr, 'the kernel no longer reaches the default limit: the test checks nothing')


if __name__ == '__main__':
    unittest.main()
