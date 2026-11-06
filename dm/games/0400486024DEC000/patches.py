# The Legend of Zelda: Ocarina of Time (0400486024DEC000): the rewriter's patches of this game's own builds. dm/tools/patch_nso.py loads every profile's patches.py and applies those of a module's NSO build ID;
# FRAME_STEP as described there ("Registered patches")
from nso_asm import ADRP, B

# Render scale (state +0x10, float written by the loader: DM_RENDER_SCALE, 1.0 by default). main+0x654e70 sizes each view: window size x the view's two scales (view +0x5328 and +0x532c, 0.75 for the scene:
# 1536x864 docked), clamped to 8..32768 and floored to the view's alignment, and hands the game's upscaler the ratio it gets. The stub multiplies both scales right after they load, so every path through the
# function gets the factor (x10, s6 and the flags are dead at main+0x654ef8: each is set again before it is read). 1.0 = bit-exact original. No frame step: the target fps stays 0 and the game keeps its own pacing
def render_scale(ctx):
    here, emit = ctx.here, ctx.emit
    ctx.site(0x654ef8, 'e70700b5')   # cbnz x7,main+0x654ff4
    start = here(); emit(ADRP(10, here(), ctx.state))
    for w in (0xbd401146,   # ldr s6,[x10,#0x10]   render scale
              0x1e2020c8,   # fcmp s6,#0.0
              0x5400006d,   # b.le skip (+12)      not set
              0x1e260800,   # fmul s0,s0,s6
              0x1e260821,   # fmul s1,s1,s6
              0xb4000047):  # skip: cbz x7,+8      (original, out of cbnz range from here)
        emit(w)
    emit(B(here(), 0x654ff4)); emit(B(here(), 0x654efc))
    ctx.put(0x654ef8, [B(0x654ef8, start)], 'view render size x the host render scale (1.0 = original)'); ctx.render_scale = 1

# Game time by the clock (the idea of Dynamic FPS by Dark, Eglantine Dev; written here from what it does). main+0x6e6cc4 sets the frame's step in game time, s8 = (60 or 30) / the game's target fps, whatever the
# frame really took, so a late game runs in slow motion. The stub keeps that step unless the measured time asks for over 3% more: elapsed ticks since the last frame, clamped to 1/15 s (as Tears of the Kingdom's stub:
# physics are not safe past that, below 15 fps the game slows down) and smoothed by 1/8, x the same 60 or 30. State +0x100 last tick, +0x108 smoothed ticks; x10-x14, s2, s3 and the flags are dead at the site
def game_time_by_clock(ctx):
    here, emit = ctx.here, ctx.emit
    ctx.site(0x6e6cc4, '2818201e')   # fdiv s8,s1,s0
    start = here(); emit(0x1e201828)   # fdiv s8,s1,s0     (original)
    emit(ADRP(10, here(), ctx.state))
    for w in (0xd53be04b,   # mrs x11,cntvct_el0
              0xf940814c,   # ldr x12,[x10,#0x100]  last tick
              0xf900814b,   # str x11,[x10,#0x100]
              0xb400030c,   # cbz x12,done (+24)    first frame
              0xcb0c016b,   # sub x11,x11,x12       elapsed ticks
              0xd53be00d,   # mrs x13,cntfrq_el0
              0xd28001ee,   # mov x14,#15
              0x9ace09ae,   # udiv x14,x13,x14      ticks in 1/15 s
              0xeb0e017f,   # cmp x11,x14
              0x9a8b81cb,   # csel x11,x14,x11,hi
              0xf940854c,   # ldr x12,[x10,#0x108]  smoothed
              0xb500004c,   # cbnz x12,+8
              0xaa0b03ec,   # mov x12,x11           first time: this frame's
              0xcb0c016e,   # sub x14,x11,x12
              0x8b8e0d8c,   # add x12,x12,x14,asr #3
              0xf900854c,   # str x12,[x10,#0x108]
              0x9e230182,   # ucvtf s2,x12
              0x9e2301a3,   # ucvtf s3,x13
              0x1e231842,   # fdiv s2,s2,s3         seconds
              0x1e210842,   # fmul s2,s2,s1         step by the clock
              0x52a7f06b,   # mov w11,#0x3f830000
              0x729ae14b,   # movk w11,#0xd70a      1.03
              0x1e270163,   # fmov s3,w11
              0x1e230903,   # fmul s3,s8,s3
              0x1e232040,   # fcmp s2,s3
              0x5400004d,   # b.le done (+8)
              0x1e204048):  # fmov s8,s2
        emit(w)
    emit(B(here(), 0x6e6cc8))
    ctx.put(0x6e6cc4, [B(0x6e6cc4, start)], 'game time by the clock when a frame runs late')

def stubs(ctx):
    render_scale(ctx); game_time_by_clock(ctx)

FRAME_STEP = {'e0f3ccfaf50065ae4ea01eb5aeb13b1a': stubs}

# How general each patch is and could be (dm/tools/docs_patches.py "Binding", listed in docs/agents/game-patches.md): what, its binding now, the loosest it could have (goal),
# purpose, why; blocked or kept: why the gap stays open (cannot close yet, or left open on purpose); checked: what was checked on game files (else a judgement)
BINDING = [
    {'what': 'render scale', 'now': 'build', 'goal': 'build', 'purpose': 'performance',
     'why': "a stub in the game's own view sizing function",
     'kept': 'one released build so far', 'checked': 'version 1.1.1: scene sizes at each factor read from the pass log'},
    {'what': 'game time by the clock', 'now': 'build', 'goal': 'build', 'purpose': 'frame rate',
     'why': "a stub at the game's own frame step",
     'kept': 'one released build so far'},
]
