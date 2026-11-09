# The Legend of Zelda: Ocarina of Time (0400486024DEC000): the rewriter's patches of this game's own code. dm/tools/patch_nso.py loads every profile's patches.py;
# MATCH_FRAME_STEP as described there ("Registered patches"). Patches of Nintendo SDK module builds are in dm/tools/sdk_patches.py
from nso_asm import ADRP, B

# The game's frame ratio. Once a frame the function at main+0x6e6be0 (v131072) divides its base rate (60.0, or 30.0 while bit 0 of [x21+0x24f] is set) by the int frame rate at
# [x21+0x2c8] (fdiv s8,s1,s0 at +0xe4) and hands the result to the time object (main+0x3b4e300: ratio at +0x10, 1 / ratio at +0x14, round(60 / ratio) at +0x1c), which the game
# steps by; the frame's own time never enters it, so a frame that takes two vsyncs at rate 60 plays at half speed. The stub gives the ratio the host's step instead: step × base / 60
# (the division's value when the host keeps frames 60 / rate vsyncs), and counts the vsyncs stepped. Until the host has decided a step, and at fps_target 0, the division runs.
# The step's unit is the state page's float at +0x200, ticks a second (0 = 60): at a 40 fps cap the host counts in ticks of 1/120 s (3 a frame), so the ratio is step × base / 120.
# The stub marks +0x204 with 1 on every pass: the host offers 40 fps only to a main whose stub reads the unit (hle_nvn.c fstep_unit). No flags are touched (cbz / cbnz only).
# s0 and x16 / x17 are free there (s0 is written again on every path before it is read; the function uses neither x16 nor x17); s1 keeps the base rate.
# Lichii (a GPL emulator; only the fact was taken) found the division (its notes: main+0x6E6CC4, 60 / [x21+0x2C8])
def frame_step(ctx):
    here, emit, fs_site, fs_put, FS_STATE_OFF, base = ctx.here, ctx.emit, ctx.site, ctx.put, ctx.state, ctx.base
    site = base + 0xe4
    fs_site(site, '2818201e')   # fdiv s8,s1,s0
    start = here(); emit(ADRP(16, here(), FS_STATE_OFF))
    for w in (0x52800031,   # mov w17,#1
              0xb9020611,   # str w17,[x16,#0x204]   mark: this stub reads the step's unit
              0xb9400211,   # ldr w17,[x16]          target fps (0 = off)
              0x340001f1,   # cbz w17,orig (+15)
              0xb9402211,   # ldr w17,[x16,#0x20]    step in ticks
              0x340001b1,   # cbz w17,orig (+13)     not decided yet
              0x1e230220,   # ucvtf s0,w17
              0x1e210808,   # fmul s8,s0,s1          step × base rate
              0xf9401a10,   # ldr x16,[x16,#0x30]
              0x8b110210):  # add x16,x16,x17
        emit(w)
    emit(ADRP(17, here(), FS_STATE_OFF))
    for w in (0xf9001a30,   # str x16,[x17,#0x30]    ticks stepped
              0xb9420230,   # ldr w16,[x17,#0x200]   ticks a second (float bits; 0 = not written)
              0x35000050,   # cbnz w16,+8
              0x52a84e10,   # mov w16,#0x42700000    60.0
              0x1e270200,   # fmov s0,w16
              0x1e201908):  # fdiv s8,s8,s0          / ticks a second
        emit(w)
    emit(B(here(), site + 4))
    emit(0x1e201828); emit(B(here(), site + 4))   # orig: fdiv s8,s1,s0; back
    fs_put(site, [B(site, start)], 'frame ratio: the host\'s step × base rate / its ticks a second (60, or 120 at a 40 fps cap) instead of base rate / the game\'s frame rate (state page target fps; 0 = the game\'s ratio)')
    dynamic_resolution(ctx)
    if base == 0x6e6be0: render_scale(ctx); canvas_scale(ctx)   # v131072 (1.1.1): their sites are this build's offsets

# The game's dynamic resolution (game__gfx__DynamicResolutionParam: LoadThreshold, ReduceCounterMax, IncreaseCounterMax and a list of levels, each a ResolutionReduceScale and a
# Priority). Its controller's update (main+0x805aa0, v131072) reads the frame's GPU time, subtracts what its cost model predicts at the current size and keeps the residual's trend;
# the level function (main+0x8065e0) adds that residual to the model's cost at each level and returns the best level that stays under the threshold, 0 (the full size, 1536x864)
# when the current size does; the update moves to a lower level after ReduceCounterMax frames that ask for it and back up after IncreaseCounterMax. Its scale divides the 3D views.
# On the Mac the GPU holds 1536x864 within a 60 fps frame, yet the controller sat at 683x384 for minutes after a loading screen (game note, Dynamic resolution). With
# DM_DYNAMIC_RESOLUTION 0 (the default; state +0x40 0) the stub makes the level function return 0, so the update climbs to full resolution and stays; 1 runs the game's function.
# The function is a leaf: x16 / x17 are free at its entry, and w0 is its only result (the original also stores the predicted cost at +0x18, which only this function and the update's
# own decision read)
def dynamic_resolution(ctx):
    here, emit, fs_site, fs_put, FS_STATE_OFF = ctx.here, ctx.emit, ctx.site, ctx.put, ctx.state
    site = ctx.others.get('dynamic resolution')
    if site is None: return   # not found in this build (a red line from the locator): the game's own dynamic resolution
    fs_site(site, '017c40bd')   # ldr s1,[x0,#0x7c]
    start = here(); emit(ADRP(16, here(), FS_STATE_OFF))
    for w in (0xb9404210,   # ldr w16,[x16,#0x40]    dynamic resolution on
              0x35000070,   # cbnz w16,orig (+3)
              0x2a1f03e0,   # mov w0,wzr             level 0: full resolution
              0xd65f03c0,   # ret
              0xbd407c01):  # orig: ldr s1,[x0,#0x7c]
        emit(w)
    emit(B(here(), site + 4))
    fs_put(site, [B(site, start)], 'dynamic resolution level: 0 (full resolution) unless state +0x40 (DM_DYNAMIC_RESOLUTION) runs the game\'s choice')
    ctx.capabilities.add('DYNAMIC_RESOLUTION')

# Render scale (state +0x10, float written by the loader: DM_RENDER_SCALE, 1.0 by default). main+0x654e70 (v131072) sizes each view: window size x the view's two scales (view +0x5328 and +0x532c, 0.75
# for the scene: 1536x864 docked), clamped to 8..32768 and floored to the view's alignment, and hands the game's upscaler the ratio it gets. The stub multiplies both scales right after they load, so every
# path through the function gets the factor (x10, s6 and the flags are dead at main+0x654ef8: each is set again before it is read). 1.0 = bit-exact original; above 1.0 the 3D views are drawn larger than
# the game's own top size (1.25 = 1920x1080), which the dynamic resolution hold above keeps at its full size
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

# Canvas scale (state +0x208, float written by the loader: DM_FRAME_BUFFER_SCALE; 0 or unset = the original). The renderer's update (v131072 main+0x6505b8) sizes its frame buffer, the 2D
# canvas the 3D views are stretched into and the HUD is drawn on, as the window's size (2560x1440 docked) times a factor from its mode table (0.8: 2048x1152), capped by the window
# (main+0x112c5e0), and hands it to main+0x1087f40; the window is then cropped to it (main+0x650560, nvnWindowSetCrop). So 2048x1152 is all the game draws docked, and a render
# scale above 1.33 could not make the 3D larger: the view sizing (main+0x654e70) never draws the 3D larger than this canvas. The stubs multiply both sizes by the host's factor
# before they are made integers (1.25 = 2560x1440, the window: real 1440p); the cap after them keeps a larger factor at the window. s2 and x16 / x17 are free at both sites (s2 is
# dead after the width's fmul, x16 / x17 are not used before the call that follows), and no flags are set (cbz only)
def canvas_scale(ctx):
    here, emit = ctx.here, ctx.emit
    for site, orig, cvt in ((0x6505c8, '1500381e', 0x1e380015), (0x6505d8, '1600381e', 0x1e380016)):   # fcvtzs w21,s0 (width) / fcvtzs w22,s0 (height)
        ctx.site(site, orig)
        start = here(); emit(ADRP(16, here(), ctx.state))
        for w in (0xb9420a11,   # ldr w17,[x16,#0x208]   canvas scale (float bits; 0 = original)
                  0x34000071,   # cbz w17,+12
                  0x1e270222,   # fmov s2,w17
                  0x1e220800,   # fmul s0,s0,s2
                  cvt):         # the original fcvtzs
            emit(w)
        emit(B(here(), site + 4))
        ctx.put(site, [B(site, start)], 'frame buffer (2D canvas) size x the host\'s canvas scale (DM_FRAME_BUFFER_SCALE; 1440p: 1.25)')

MATCH_FRAME_STEP = {'main': [
    {'name': 'frame ratio from the host\'s step', 'size': 0x9b4, 'fingerprints': ['6ae2aff5344d14568e4dbaf93455e2d8278384bf892ed6c6eca87fef98fbcd1d'], 'stub': frame_step,   # v131072 main+0x6e6be0
     'others': {'dynamic resolution': {'size': 0x294, 'fingerprints': ['7af6ceb0aedf8e0c17dc08ce17fcd6a7cba6d394a0d09e3b504c6d668dc418e7']}}},   # v131072 main+0x8065e0
]}

BINDING = [
    {'what': 'frame ratio from the host\'s step', 'now': 'game', 'goal': 'game', 'purpose': 'frame rate',
     'why': "the game's own frame-ratio division, found by its function's fingerprint on any build that keeps the function",
     'checked': 'v131072 main: one function with this fingerprint, the site and its free registers read by disassembly (2026-11-08)'},
    {'what': 'dynamic resolution held at full size', 'now': 'game', 'goal': 'game', 'purpose': 'rendering',
     'why': "the game's own dynamic-resolution level function, found by its function's fingerprint on any build that keeps the function; switchable (DM_DYNAMIC_RESOLUTION)",
     'checked': 'v131072 main: one function with this fingerprint (a leaf, x16 / x17 free), its callers and the controller read by disassembly (2026-11-08)'},
    {'what': 'render scale', 'now': 'build', 'goal': 'game', 'purpose': 'rendering',
     'why': "a stub in the game's own view sizing function, at v131072's offset",
     'kept': 'one released build so far', 'checked': 'version 1.1.1: scene sizes at each factor read from the pass log'},
]
