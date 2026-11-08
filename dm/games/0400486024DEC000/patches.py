# The Legend of Zelda: Ocarina of Time (0400486024DEC000): the rewriter's patches of this game's own code. dm/tools/patch_nso.py loads every profile's patches.py;
# FRAME_STEP and MATCH_FRAME_STEP as described there ("Registered patches"). Patches of Nintendo SDK module builds are in dm/tools/sdk_patches.py
from nso_asm import ADRP, B

# The game's frame ratio. Once a frame the function at main+0x6e6be0 (v131072) divides its base rate (60.0, or 30.0 while bit 0 of [x21+0x24f] is set) by the int frame rate at
# [x21+0x2c8] (fdiv s8,s1,s0 at +0xe4) and hands the result to the time object (main+0x3b4e300: ratio at +0x10, 1 / ratio at +0x14, round(60 / ratio) at +0x1c), which the game
# steps by; the frame's own time never enters it, so a frame that takes two vsyncs at rate 60 plays at half speed. The stub gives the ratio the host's step instead: step × base / 60
# (the division's value when the host keeps frames 60 / rate vsyncs), and counts the vsyncs stepped. Until the host has decided a step, and at fps_target 0, the division runs.
# s0 and x16 / x17 are free there (s0 is written again on every path before it is read; the function uses neither x16 nor x17); s1 keeps the base rate.
# Lichii (a GPL emulator; only the fact was taken) found the division (its notes: main+0x6E6CC4, 60 / [x21+0x2C8])
def frame_step(ctx):
    here, emit, fs_site, fs_put, FS_STATE_OFF, base = ctx.here, ctx.emit, ctx.site, ctx.put, ctx.state, ctx.base
    site = base + 0xe4
    fs_site(site, '2818201e')   # fdiv s8,s1,s0
    start = here(); emit(ADRP(16, here(), FS_STATE_OFF))
    for w in (0xb9400211,   # ldr w17,[x16]          target fps (0 = off)
              0x340001b1,   # cbz w17,orig (+13)
              0xb9402211,   # ldr w17,[x16,#0x20]    step in vsyncs
              0x34000171,   # cbz w17,orig (+11)     not decided yet
              0x1e230220,   # ucvtf s0,w17
              0xf9401a10,   # ldr x16,[x16,#0x30]
              0x8b110210):  # add x16,x16,x17
        emit(w)
    emit(ADRP(17, here(), FS_STATE_OFF))
    for w in (0xf9001a30,   # str x16,[x17,#0x30]    vsyncs stepped
              0x1e210808,   # fmul s8,s0,s1          step × base rate
              0x52a84e10,   # mov w16,#0x42700000    60.0
              0x1e270200,   # fmov s0,w16
              0x1e201908):  # fdiv s8,s8,s0          / 60
        emit(w)
    emit(B(here(), site + 4))
    emit(0x1e201828); emit(B(here(), site + 4))   # orig: fdiv s8,s1,s0; back
    fs_put(site, [B(site, start)], 'frame ratio: the host\'s step × base rate / 60 instead of base rate / the game\'s frame rate (state page target fps; 0 = the game\'s ratio)')


# Dynamic-resolution ceiling (PR #1497). The engine stores its final scale after dynamic and fixed-mode selection (str s0,[x20,#0xe5c] at main+0x31d3f48); state +0x10 holds the
# host's DM_RENDER_SCALE: below 1.0 the stored scale is the smaller of the game's and the host's, so the game can still choose a lower one
def render_scale(ctx):
    site = 0x31d3f48
    ctx.site(site, '805e0ebd')  # str s0,[x20,#0xe5c]
    start = ctx.here()
    ctx.emit(0xadbf7ffe)  # stp q30,q31,[sp,#-32]!
    ctx.emit(0xa9bf47f0)  # stp x16,x17,[sp,#-16]!
    ctx.emit(ADRP(16, ctx.here(), ctx.state))
    for word in (
        0xd53b4211,  # mrs x17,nzcv: the original store leaves flags intact
        0xbd40121e,  # ldr s30,[x16,#0x10]: host scale ceiling
        0x1e2023c8,  # fcmp s30,#0
        0x540000ed,  # b.le original: an unset state keeps the game
        0x1e2e101f,  # fmov s31,#1
        0x1e3f23c0,  # fcmp s30,s31
        0x5400008a,  # b.ge original: 1.0 keeps the original bits
        0x1e3e581f,  # fmin s31,s0,s30: the game can still choose a lower scale
        0xbd0e5e9f,  # str s31,[x20,#0xe5c]: leave s0 as the original store did
        0x14000002,  # b restore
        0xbd0e5e80,  # original: str s0,[x20,#0xe5c]
        0xd51b4211,  # restore: msr nzcv,x17
        0xa8c147f0,  # ldp x16,x17,[sp],#16
        0xacc17ffe,  # ldp q30,q31,[sp],#32
    ):
        ctx.emit(word)
    ctx.emit(B(ctx.here(), site + 4))
    ctx.put(site, [B(site, start)], 'dynamic resolution capped by the host render scale (1.0 = original)')
    ctx.render_scale = 1


# Fixed resolution (DM_FIXED_RESOLUTION, state +0x60; its scale DM_FIXED_SCALE, state +0x64 as float bits, 0 = the game's 1.0). The engine's resolution controller
# (main+0x805b00…) reads its config flag at [x8+0x94] and, when bit 0 is clear, takes the path at main+0x80640c that renders the 3D at a fixed scale (mov w9,#0x3f800000 at
# main+0x806420, 1.0): "dynamic resolution disabled". Bit 0 set (the shipped configuration) follows the GPU-time controller, which on a Mac whose translated frame costs
# more than 16.7 ms settles at the floor (911x512 docked). The first stub takes the fixed path also when state +0x60 is set; the second gives that path the host's scale when
# state +0x64 holds one (0.5 to 1.0 as float bits), so 75 / 67 / 50 % of the top size are possible without the controller. Found as a 4-byte patch by Lichii ("Max Resolution",
# main+0x805BD0: tbz → b) and as a runtime switch by Hookshot (nx2/oot_patches.py: the flag at +0x94, the fixed path's scale); only the facts were taken.
# x16 / x17 are saved around both stubs: nothing says they are free in these functions. Neither stub touches the flags
def fixed_resolution(ctx):
    site = 0x805bd0
    ctx.site(site, 'e8410036')  # tbz w8,#0,#0x80640c
    start = ctx.here()
    ctx.emit(0xa9bf47f0)  # stp x16,x17,[sp,#-16]!
    ctx.emit(ADRP(16, ctx.here(), ctx.state))
    for word in (
        0xb9406211,  # ldr w17,[x16,#0x60]: 1 = fixed resolution
        0x35000091,  # cbnz w17,fixed (+4)
        0x36000068,  # tbz w8,#0,fixed (+3): the game's own choice
        0xa8c147f0,  # ldp x16,x17,[sp],#16
    ):
        ctx.emit(word)
    ctx.emit(B(ctx.here(), site + 4))   # dynamic: the instruction after the tbz
    ctx.emit(0xa8c147f0)  # fixed: ldp x16,x17,[sp],#16
    ctx.emit(B(ctx.here(), 0x80640c))
    ctx.put(site, [B(site, start)], 'fixed resolution: the "dynamic resolution disabled" path also when state +0x60 is set')
    site2 = 0x806420
    ctx.site(site2, '09f0a752')  # mov w9,#0x3f800000: the fixed path's scale, 1.0
    start2 = ctx.here()
    ctx.emit(0xf81f0ff0)  # str x16,[sp,#-16]!
    ctx.emit(ADRP(16, ctx.here(), ctx.state))
    for word in (
        0xb9406609,  # ldr w9,[x16,#0x64]: the host's scale as float bits (0 = unset)
        0x35000049,  # cbnz w9,done (+2)
        0x52a7f009,  # mov w9,#0x3f800000: the game's 1.0
        0xf84107f0,  # done: ldr x16,[sp],#16
    ):
        ctx.emit(word)
    ctx.emit(B(ctx.here(), site2 + 4))
    ctx.put(site2, [B(site2, start2)], 'fixed resolution: its scale from state +0x64 (0 = the game\'s 1.0)')
    ctx.fixed_resolution = 1


def v131072(ctx):   # one build, every stub: FRAME_STEP of a build takes the place of the function-matched frame step, so the frame ratio stub runs from here with its function's base
    ctx.base = 0x6e6be0
    frame_step(ctx)
    render_scale(ctx)
    fixed_resolution(ctx)


FRAME_STEP = {'e0f3ccfaf50065ae4ea01eb5aeb13b1a': v131072}   # 1.1.1 (v131072)

MATCH_FRAME_STEP = {'main': [
    {'name': 'frame ratio from the host\'s step', 'size': 0x9b4, 'fingerprints': ['6ae2aff5344d14568e4dbaf93455e2d8278384bf892ed6c6eca87fef98fbcd1d'], 'stub': frame_step},   # v131072 main+0x6e6be0; any other build that keeps the function
]}

BINDING = [
    {'what': 'frame ratio from the host\'s step', 'now': 'game', 'goal': 'game', 'purpose': 'frame rate',
     'why': "the game's own frame-ratio division, found by its function's fingerprint on any build that keeps the function",
     'checked': 'v131072 main: one function with this fingerprint, the site and its free registers read by disassembly (2026-11-08)'},
    {'what': 'dynamic-resolution ceiling', 'now': 'build', 'goal': 'game', 'purpose': 'rendering',
     'why': "the game's final scale store follows its dynamic and fixed-mode choices",
     'blocked': 'the store and its role are verified for this main build only',
     'checked': 'v131072 main E0F3CCFAF50065AE4EA01EB5AEB13B1A, original store and surrounding code, 2026-11-06'},
    {'what': 'fixed resolution and its scale', 'now': 'build', 'goal': 'game', 'purpose': 'rendering',
     'why': "the resolution controller's config flag and the fixed path's scale constant",
     'blocked': 'the flag read and the scale constant are verified for this main build only',
     'checked': 'v131072 main E0F3CCFAF50065AE4EA01EB5AEB13B1A, the tbz at +0x805bd0 and the fixed path at +0x80640c read by disassembly (2026-11-08)'},
]
