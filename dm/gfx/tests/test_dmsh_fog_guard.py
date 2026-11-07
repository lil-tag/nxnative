#!/usr/bin/env python3
# The fog tolerance and the loop guard of dmsh_build.py, on the shapes they meet.
import pathlib, re, sys
src = (pathlib.Path(__file__).parent.parent / 'tools' / 'dmsh_build.py').read_text(errors='replace')
ns = {'re': re, 'sys': sys}
exec(src[src.index('LOOP_CAP = 1 << 18'):src.index('# Ampere', src.index('FOG_TOLERANCE')) if '# Ampere' in src[src.index('FOG_TOLERANCE'):] else None].split('\n\n#')[0], ns)
exec(src[src.index('FOG_TOLERANCE = {'):src.index('\n\n', src.index('def fog_tolerance'))], ns)
fog, guard = ns['fog_tolerance'], ns['loop_guard']
msl = 'void f()\n{\n    bool _126 = _118 > _124;\n    float _128 = _98;\n    if (_126)\n    {\n        discard_fragment();\n    }\n    bool _9 = _1 > _2;\n}\n'
out = fog('5ad65c0eefd09c7f', msl)
assert 'bool _126 = _118 > (_124 + 4e-6f);' in out, out
assert 'bool _9 = _1 > _2;' in out, 'only the test that decides the discard changes'
assert fog('0000000000000000', msl) == msl, 'other shaders stay as they are'
g = guard('void f()\n{\n    for (;;)\n    {\n    }\n    while (a)\n    {\n    }\n    do\n    {\n    } while (b);\n    for (int i = 0; i < 4; i++)\n    {\n    }\n}\n')
assert g.count('dm_lg') == 6 and 'for (; ++dm_lg < 262144u;)' in g and '} while ((b) && ++dm_lg < 262144u);' in g, g
assert guard(g) == g, 'a guarded source is left alone'
plain = 'struct S\n{\n    int a;\n};\nvoid f(int x)\n{\n    x = 1;\n}\n'
assert guard(plain) == plain, 'a shader with no loop keeps its text, and its compiled library'
print('fog tolerance and loop guard: ok')
