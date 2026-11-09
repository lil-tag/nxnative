# Ocarina of Time guide

The Legend of Zelda: Ocarina of Time, the Switch 2 remake, version 1.1.1, on a Mac. Its title ID, the code that names its files and folders, is `0400486024DEC000`. This guide covers the `ocarina-7` branch, which is the build I play, and `ocarina-8`, the next one.

[Back to the README](../README.md)

## In short

- **How it runs:** about 37 to 43 frames per second on the test walk, on an M4 Mac mini with 24 GB. Upstream reports a locked 60 fps in Kokiri Forest on an M6, a newer and faster chip.
- **Where I've played it:** Kokiri Forest, the Deku Tree up to and past its boss, Hyrule Field and Castle Town. Later areas haven't been played on this branch.
- **What changed:** freezes on first visits went from up to 16 seconds to under 1 second, launches after the first prepare in about 3 seconds instead of 6 minutes, movies play at full speed, and fixes for a white cloud, the missing flame and fog, and several crashes. Details below.

**The test walk.** Every number in this guide comes from the same walk on the same save, from Kokiri Forest to Hyrule Field, on the M4 Mac mini, unless it says otherwise. Frames per second are counted from the time between saved pictures.

## Which branch

| Branch | State |
|---|---|
| `ocarina-7` | The build I play. Upstream's code from 8 November 2026, plus upstream pull requests that weren't merged yet, plus this fork's own fixes. |
| `ocarina-8` | `ocarina-7` plus HDR output and a lighting fix. Still being tested. |
| `ocarina-2` | An older attempt, on upstream from 5 November. Kept for history only. |

## Build it

Follow [Build a game](../README.md#build-a-game) in the README, with the `ocarina-7` branch.

- **Your copy of the game:** a `.nup` file, or a folder with the base game and the 1.1.1 update. Only version 1.1.1 works: with the base game, add the 1.1.1 update.
- **Your Mac:** I've only tested 24 GB of memory. Upstream played it on an M6 with 16 GB.
- **Time and space:** the first build took 2 hours 20 minutes on the M4 and about 62 GB, most of it shaders.
- **First launch:** the first time you open a newly built app, macOS may scan it for several minutes (about 5 on my Mac), and it looks like nothing is happening. Then the game spends several minutes on "Preparing shaders". Later launches prepare in seconds.
- **Check your copy.** Some copies of this game going around have damaged files (see [damaged files](#damaged-game-files)). The build stops if it finds damaged textures; `--allow-damaged` lets it go on. A clean copy needs none of the workarounds.

To avoid the short freezes the first time an enemy or effect appears, you can build all the game's graphics programs for those once, ahead of time (in Metal's words, every mesh and compute pipeline): run the game once with `DM_PSO_PREBUILD=extra`. It builds them and exits. On the M4 that took about 15 minutes and 1.2 GB.

## Settings

These are the settings you can change for this game. Most appear in the settings window before the game starts, and the ones marked "live" also in the game window's Game menu while you play. The others go in the game's `env.txt` file, as the [README](../README.md#everything-this-fork-adds) explains.

| Setting | Variable | What it does |
|---|---|---|
| Frame rate (live) | `DM_FPS_TARGET` | 30 or 60 frames per second. 30 is steadier on an M4. |
| Hold frames when late (live) | `DM_FPS_HOLD` | When a frame is late, the game waits for a whole extra screen refresh, so motion stays even but the rate can drop to 20. On by default. On the test walk, on the earlier `ocarina-5` branch, it gave 27 fps with it on and 31 with it off. |
| Render scale (live) | `DM_RENDER_SCALE` | The size the 3D picture is drawn at. Below 1.0 is softer; above 1.0, up to 2.0, draws more than the game's own top size and is sharper. In our tests 0.83 drew 1304×734 and 1.25 drew 1951×1098. On the M4 it changes the frame rate less than you'd expect, because most of the time goes to the number of things drawn, not their size. |
| Dynamic resolution (live) | `DM_DYNAMIC_RESOLUTION` | Lets the game lower its resolution when busy, as on the console. Off by default here. |
| Fixed resolution | `DM_FIXED_RESOLUTION`, `DM_FIXED_SCALE` | Keeps the 3D at one size, at the scale you choose: sharper, and the frame rate drops instead. |
| Console mode (live) | `DM_OPMODE` | Docked or handheld. Handheld draws a smaller picture and runs a little faster. |
| Game's upscaler and Sharpening | `DM_UPSCALER`, `DM_SHARPEN` | Replace the game's own upscaler with MetalFX, or skip it, and sharpen the result. I haven't confirmed that it changes this game's picture: on an earlier branch it never ran for this game. |
| HDR output (`ocarina-8`) | `DM_HDR` | Sends the game's HDR picture to an HDR screen. Still being tested. |
| Microphone | `DM_MIC`, `DM_MIC_GAIN` | Uses the Mac's microphone as the game's, to play the ocarina into it. |
| Frame-rate counter | `DM_SHOW_FPS` | Frames per second and time per frame, top left. |
| Play in the background | `DM_BACKGROUND_PLAY` | Keeps the game and controller running while another app is in front. |

Settings for troubleshooting:

| Variable | What it does |
|---|---|
| `DM_PSO_PREBUILD=extra` | Builds every mesh and compute pipeline once, then exits. |
| `DM_NVN_RES_LIMIT_MB=<MB>` | Sets the memory guard's limit yourself, instead of about 95% of what Metal allows. |
| `DM_ROMFS_ZSTD_CHECK=0` | Turns off the damaged-file substitutes. |
| `DM_NVN_NDVE_MOVIE_DIRECT=0` | Plays movies through the game's own slow path again. |
| `DM_NVN_FINITE_GUARD=0` | Turns off the guard against broken pixels behind the white cloud. |
| `DM_CTL=<file>` | Lets a script press buttons, take screenshots and change settings while the game runs. |

## What changed, and why

Each item says what you'd see in the game, then what fixed it and where the fix came from. "This fork" means work done here; a number like #1391 is an upstream pull request.

### Freezes and slow launches

- **The game froze for seconds the first time you met an enemy, used an item or entered an area.** The Mac was building the graphics programs for it on the spot, one at a time, on the thread that feeds the GPU. Now:
  - mesh pipelines are built before their first draw and remembered for the next launch (#1368);
  - the pipeline archive keeps half-built versions of mesh programs, so a missing one is finished in under half a millisecond instead of built from scratch (this fork);
  - `DM_PSO_PREBUILD=extra` can build all mesh and compute pipelines ahead of time (this fork).

  On the test walk, starting with nothing built yet: pauses longer than a quarter of a second went from 23 (74.5 seconds in all) to 4 (2.5 seconds), and the longest from 16.6 seconds to 0.97.
- **"Preparing shaders" took about 6 minutes on most launches.** After each play session the game rebuilt all 11,000 of its saved pipeline pieces, about 3 minutes of work on every processor core. Now only the files added since last time are rebuilt (from swordboiMacNative). A second launch prepares in 3.4 seconds.
- **The shader prebuild sometimes crashed.** Two builds of the same program at once could read memory Metal had just freed (upstream issue #815). They now take turns (this fork).
- **Castle Town was very slow the first time.** With many pipelines waiting to be built, every draw searched every archive file again: 67,011 failed searches in 300 frames. A missing piece is now searched for once (this fork).

### Picture

- **A white cloud at the forest exit.** One fog shader multiplied infinity by zero. The Switch's GPU treats that as 0; Metal makes it "not a number", and the blur spread it. The shader now uses the Switch's rule when it's built, and a guard clears any broken pixels left at the end of that pass (this fork).
- **No flame on a lit Deku Stick, and missing ground fog.** The translator put a shader's depth output in the wrong place. Fixed upstream (473c9026, the bug reported in #1292).
- **Specks in the fog.** A depth comparison failed by a hair. Fixed by #1742, which replaced this fork's earlier fix.
- **Wind, leaves and particles moved about 20 times too fast.** The game reads the GPU's clock at the Switch 2's rate, not the Switch's (#1714, by padsiwono).
- **Draws that read depth before it was stored.** On Apple's GPUs a draw that read the depth the same pass had just written could read old values; it now starts a new pass (from #1590).
- **The GPU's work was cut into too many pieces.** About 18,000 extra breaks between render passes on the test walk, gone with #1729 (by K. Solen). One bug in it is fixed here: as posted, it broke every pass whenever the game used a memory pool, which is almost always. Frame rate on the walk went from 33 to 41 fps.
- **A bright band on a dark wall in Gohma's room** (`ocarina-8`). Fixed upstream (e4e5ff64) by compiling translated shaders with wrap-around integer maths (`-fwrapv`).
- **Blurry picture from a GPU timing setting.** Upstream's profile measures GPU time in a way that, on an M4, made the game drop to 683×384. This fork leaves it out and keeps 1536×864.

### Movies

- **Movies played at half speed and out of step with their sound.** The game's movie library ran 14 GPU passes per picture, 22 to 30 ms in all. The decoded picture is now scaled straight to the screen on Metal (#1331): 16.7 ms a frame, GPU 3.5 ms.
- **When the game is slower than a movie,** late pictures are skipped instead of the movie playing slow (#1379).

### Memory and crashes

- **The Mac could freeze when graphics memory ran out.** A guard stops the game with an alert before that happens (#1391). It used to read Metal's counter, which counts shared memory several times: it stopped the game at 17.4 GB when 6.9 GB was really in use. It now reads what's really held (#1482). After that, a 15-minute session on my save ran with no stop.
- **Black screen leaving the Deku Tree, in long sessions.** The game ran out of address space. It now gets 16 GB, as #1444 and #1590 found.
- **Render targets used 3 GB of memory.** They now share one block, 0.7 GB (#1453).
- **The game froze for 6.5 hours after a crash.** The crash handler waited on a thread that never answered. It now exits after 15 seconds (this fork).
- **Crashes at the ocarina's first note, entering Deku Tree rooms and entering Hyrule Field,** reported by other players. Fixed by upstream, plus null-pointer guards carried from #1444.
- **Retired graphics buffers piled up** when the game stopped sending work (#1392).

### Damaged game files

Some copies of this game have files that start with zeros. With them, the game tries to load the Kokiri Forest fairy fountain model forever, about 22 times a second, using about two cores. #1530 serves a harmless stand-in when a texture file is damaged: the processor use fell from 412% to 180%. Upstream closed that pull request, since the real fix is a clean copy; it's kept here for players who have only a damaged one.

A copy I compared had 9 damaged files: 3 textures, plus baked lighting for Hyrule Field and Zora's Fountain, two German voice lines and one settings file. The stand-ins only cover the textures, so a clean copy is better.

### Controls and the Mac window

- **Game menu** in the window's menu bar: frame rate, "Hold frames when late", render scale, dynamic resolution and console mode, changed while you play (this fork).
- **The camera drifted left with a Pro Controller.** The gyro's zero is now learned from a second at rest (from swordboiMacNative). One Pro Controller also counts as one pad, not two (#1722).
- **Frame-rate counter** with the time per frame, as on the iPhone (this fork).
- **Window > Metal HUD** (⌘⇧H) shows Apple's performance overlay (from swordboiMacNative).
- **Play in the background** (this fork).
- **Microphone input**, to play the ocarina into the Mac (#1662).

### Speed in the game's shaders

The shader translator was rebuilt with upstream's faster memory reads: 128-bit loads in one step, plus #1585, #1752 and #1813. Upstream measured 34.7 to 51.8 fps on an M4 Pro from the 128-bit loads alone.

## Measurements

The test walk on the M4 Mac mini with 24 GB, with "Hold frames when late" off.

| Branch | Frames per second | GPU time per frame |
|---|---|---|
| `ocarina-5` (upstream from 8 November plus this fork's fixes, before #1729) | 32.9 | 25.8 ms |
| `ocarina-6` (adds #1729) | 41.0 and 41.2 | 21.3 ms |
| `ocarina-7` (adds swordboiMacNative's pull requests) | 37.5 and 43.3 | 21.8 and 20.4 ms |

The two `ocarina-7` runs differ by 6 fps, so one run isn't enough to rank the last two branches.

## What's still wrong

- **Render scale stops at 2048×1152.** The 1440p choice has never drawn more than that, at launch or live. Cause not found yet.
- **The picture flickers in brightness while the camera pans.** It didn't with the resolution held still, so "Fixed resolution" may help.
- **Castle Town's market is heavy:** 2,000 to 3,300 draws a frame, 23 to 30 fps when measured on an older branch, before the faster shaders above.
- **Memory grew on a still screen once,** by 192 KB a frame for 16 minutes, until the guard stopped the game. A 36-minute test didn't repeat it. Cause not found.
- **One crash in the sound thread** on the first `ocarina-7` test run. It didn't happen again in 6 later runs.
- **Water in the Deku Tree's basement sparkles a little.** It did on every build.
- **No multi-frame upscaling or frame generation.** Both need motion vectors, a record of which way each pixel moved since the last frame, and this game doesn't draw them.

## Being worked on now

On `ocarina-8`:

- **HDR output** for an HDR screen: merged from #1176, being tested.
- **A 40 fps choice,** between 30 and 60: being written.
- **Real 1440p:** finding what stops render scale at 2048×1152.

## Credits

- **Upstream's Ocarina port:** seedds, and ChiefSZ13 (#1299), stan69b, aCidcOde, fumeware and lunks, whose work started it.
- **[swordboiMacNative](https://github.com/Fasen24-AI/swordboiMacNative)** (Fasen24-AI, commits by EmmanueleC): its collection of upstream pull requests, the faster launches and the gyro fix.
- **Pull request authors:** K. Solen (#1444, #1729, #1731), padsiwono (#1714), cocoman93 (#1707), v0lumehi (#1708), Max Davidson (#1176, HDR), beecrepaldi-afk and b1tdr3amer24 (HDR on the Mac), and the authors of #1331, #1368, #1379, #1391, #1392, #1453, #1482, #1530, #1585, #1590, #1662, #1722, #1742, #1752 and #1813.
- **Forks I compared against:** Aznracer's [Mighty-Green-Kokiri-Kid](https://github.com/Aznracer/Mighty-Green-Kokiri-Kid) and cheemu21's [kid-with-sword-remake](https://github.com/cheemu21/kid-with-sword-remake).
