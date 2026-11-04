# Mario Tennis Fever (`0400D4201C6EA000`)

A Switch 2 game: update v327680 (1.1.1) over the base game, SDK 20.5.17 built for the Switch 2 (`Ounce`), NVN2. Camelot's engine (`nxe`) on NintendoWare (G3d, Ui2d, Vfx, Atk), with Enlighten, Pia and NPLN. Modules `main` and `sdk`, plus one NRO that `main` decodes at run time. **Plays on the Mac** (M4 Pro): the title movie, the practice, the main menu, Adventure's opening story, and Free Play singles on Stadium Court (Grass) with real rallies, at 60 fps. On an M4 too a match holds 60 fps at 2048x1152 (below). No IPA has been built.

## What it needs

| Setting | Where | Why |
|---|---|---|
| `mem_alias 0x500000000`, `mem_reserve 0x650000000` | [`game.tsv`](game.tsv) | `nninitStartup` asks `StandardAllocator` for 16 GB of address space, and this SDK keeps its 1 GB node area (no node-area cut matches it): with a 16 GB alias region `CentralHeap::Init` aborted (`sdk+0x13f698`) |
| `mem_stack 0x10000000`, `app_pool 0x240000000` | `game.tsv` | Splatoon Raiders' values, the Switch 2's 9 GB, not measured here |
| `sys_resource 0x2000000` | `game.tsv` | the NPDM declares 32 MB, so virtual address memory is on |
| `render_cond on` | `game.tsv` | as the console does. Off, a rally looks the same and takes about 0.7 of 9.5 ms more GPU (one run each, with another job on the Mac) and logs a red line |
| `pad_layout full` | `game.tsv` | the game asks for most of the controller |
| `image_write_late on` | `game.tsv` | its render targets carry the image flag and shaders write 9 of them: [made shader-writable they lose lossless compression](../../../docs/reference/rendering.md#render-targets-flagged-as-images-lose-lossless-compression), 17.9 against 14.4 ms of GPU in a match |
| `fb_fresh_pass on` | `game.tsv` | [the atmosphere reads what the lights just drew](../../../docs/reference/rendering.md#a-feedback-snapshot-reused-by-the-next-pass-with-no-barrier-between), with no barrier between: without it, Adventure is dark |

The NRO and the archive need no key: the NSP script captures one and `shader_split.py` reads the other (below).

## Files

- A dump folder (`base/`, `updates/327680/`): `build_ipa_from_nsp.py <folder> --platform mac` packs it with no keys. The display version `v327680` is a placeholder, as a dump has no control data.
- `main` has about 600 KB of code. It is a boot stub (`nxboot`): six threads unscramble and unpack the game's code, a 24.7 MB NRO, out of its data segment, and it loads that with `nn::ro` ([A game that decodes its code at run time](../../../docs/reference/services.md#a-game-that-decodes-its-code-at-run-time)). The NSP script captures it from a boot in about 2 s. Its SHA-256 is the one the RomFS's NRR registers ([NRR version 1](../../../docs/reference/services.md#nrr-version-1)). The stub obfuscates its strings and streams, but runs as it is: no game code is changed for this.
- The RomFS is `42` (7.6 GB, magic `CRA.`) and the NRR. `42` holds 8,671 files, listed only in the NRO: 5,474 sound, 164 WebM movies, the rest NARC containers of FRES, FSHA, BNTX, VFXB, SARC ([One scrambled archive indexed by the NRO](../../../docs/reference/shaders.md#one-scrambled-archive-indexed-by-the-nro)). `dm/gfx/tools/cra.py <42> <NRO> <folder>` writes them out.
- Movies: VP9 WebM with Vorbis, up to 2560x1440 at 60 fps; the title movie is 368 MB. The game reads them itself from the archive and feeds the SDK's extractor (`AppendMediaSourceData`).

## Shaders

35,045 in [`shader_map.tsv`](shader_map.tsv): 26,096 from models (one FSHA each), 5,331 from `ssg_system`, 3,316 from effects, 138 from `shader/`, 124 from layouts, 40 in the NRO's data. All translate. A full build takes about an hour on an M4 Pro with another job running (not timed from an empty cache in one go). [`pso_manifest.tsv`](pso_manifest.tsv) (1,351 lines) is one Mac session: boot, the movie, the menus, a match.

One does not load (current): `3032440e5027bbe5`, `shader/audience/audience_compute`, reads `array<texture2d<float>, 32> cp_t2d_cb0_0`, the compute texture handles of NVN2's per-shader `c[0]` indexed by its workgroup ID. `dmsh_build.py` `c2_array` only knows c2; fixing it is a chain change. The dispatch is skipped, a red line says so, and the crowd still draws; what the shader adds has not been seen.

## Rendering

- The scene renders at 2048x1152 into a 2560x1440 window texture, and the game crops the window to that (`nvnWindowSetCrop`). `DM_NVN_SHOT` takes the whole texture, so shots have a black band on the right and bottom; the window does not.
- Deferred: albedo and normals, then lighting into R11G11B10F, the atmosphere over it, bloom, an ACES tonemap. The atmosphere's table is a 3D texture of 8x8x64, filled once per stage with one draw per plane through a geometry shader that picks the layer ([A passthrough geometry shader that writes the layer](../../../docs/reference/shaders.md#a-passthrough-geometry-shader-that-writes-the-layer): the blue wash).
- Frame time on an M4 in Free Play at 2048x1152, one run each on a quiet Mac: start of the night 26 to 29 ms (GPU 26 to 32 ms, 249 passes); with the exact overlap check, lazy clears and the cached intervals about 19 ms (GPU about 18 ms, 113 passes); with `image_write_late` 16.66 ms, p95 17.5, worst 19 ms (GPU 14.4 ms, 167 passes), a steady 60 fps. That holds overnight only: in daytime use, with the window server at about 60% of a core, three runs gave 17 to 18.7 ms (GPU about 16 ms, about 175 passes) and in the two compared directly about a quarter of the frames stayed on screen too long. Why is not measured; the busy Mac is a guess. The number of passes follows the crowd: the game draws 12 to 33 of its 128x128 pictures a frame, three passes each, and which it chooses in a run is not understood.
- Adventure's town: 22 to 26 ms a frame (GPU 22 to 24 ms), about 2.9 GB read and 1.6 GB written in attachments a frame; the academy's hall 20 to 21 ms. Neither reaches 60.
- Tried and no gain in a match: `DM_NVN_BARRIER=game`, `DM_NVN_TBDR=1`, `DM_NVN_ENC_THREAD=1`, `DM_NVN_CUT` at 8, 16, 32 or 64 draws. `DM_RES_SCALE` below 1 does not apply, as the big targets are images. Handheld mode (`DM_OPMODE=0`, 1920x1080) was 17.65 ms before `image_write_late`, not measured since.

## Driving it on the Mac

The work folder `0400D4201C6EA000-work` beside the dump has `run/boot.sh <log> <secs> [VAR=value…]`, `run/drive.py` (control lines, `wait S`, `snap NAME`), `run/tomatch.py <log>` (from a save, to the first serve of a Free Play match in about 100 s) and `run/rally.py`. With no save, A from 25 s goes through the movie into the practice, and Plus twice leaves it for the main menu. The stick moves the menus (`LDown`). A tosses and hits. The opponent and court are the menu's defaults or random.

## Open

- **Tessellated grass is not drawn.** `shader/grass` and `shader/sea_of_clouds` use hull and domain shaders (two each), which the host has no path for: `primitive type 14 not implemented` and `geometry draw cannot capture this pipeline` (tessellation with a geometry shader), and those draws are skipped. The court's own texture and stripes draw, so grass courts look flat; compared with nothing yet.
- `3032440e5027bbe5` above.
- **The title movie repeats a picture now and then.** Measured with temporary timing in `hle_codec_video.c` on a quiet Mac: of 7,044 steps between pictures in the 118 s movie, 7,006 take 16 to 18 ms, 14 take 30 to 34 ms and 3 take 48 to 51 ms (one run; 33 long ones in another), 8 of them in the first 6 s. The frames themselves stay on time (0 dropped on screen). The picture timeline ends 284 to 660 ms behind the clock, so the player's own clock falls behind. Decoding is not the cause: 3 pictures wait ready, and decoding outside the decoder's lock did not help (17 long steps became 33, reverted). Guess, not tested: the movie's audio clock. It needs the player's loop read in the NRO.
- A `GPU Hang` / MMU interrupt report naming WindowServer every 8 to 10 s in 2 of about 40 runs, not fatal, from before this night's changes; not traced to this game.
- Nothing past the first Adventure scenes and one Free Play match has been played: no other court, doubles, the online modes (no network on the host).
- Not checked: game speed against the console; the host's `IsRunningOnOunce` answers false.
- `linear texture storage is not in a memory pool (1024x1024 format 0x26)`, once in some runs, no effect seen.
- An IPA: the 20 GB alias region is over iOS's limit until this SDK gets a node-area cut ([An SDK 21 Switch 2 game with a 4.5 GB alias region](../../../docs/reference/memory.md#an-sdk-21-switch-2-game-with-a-45-gb-alias-region)).
- [Stack slots reused after `ro::Initialize`](../../../docs/reference/threads.md#a-threads-arguments-in-slots-its-starter-reuses-after-connecting-to-a-service) are held off by a modelled time, not an order.

## Found on this game

- [A game that decodes its code at run time](../../../docs/reference/services.md#a-game-that-decodes-its-code-at-run-time) and [NRR version 1](../../../docs/reference/services.md#nrr-version-1).
- [One scrambled archive indexed by the NRO](../../../docs/reference/shaders.md#one-scrambled-archive-indexed-by-the-nro).
- [A shared font larger than the game's font engine memory](../../../docs/reference/services.md#a-shared-font-larger-than-the-games-font-engine-memory): a null pointer in `nn::font` at boot.
- [A thread's arguments in slots its starter reuses after connecting to a service](../../../docs/reference/threads.md#a-threads-arguments-in-slots-its-starter-reuses-after-connecting-to-a-service): a crash at boot about once in a dozen runs.
- [The submission-ready event counted as packets](../../../docs/reference/movies.md#the-submission-ready-event-counted-as-packets): black movies, then corrupt frames.
- [A passthrough geometry shader that writes the layer](../../../docs/reference/shaders.md#a-passthrough-geometry-shader-that-writes-the-layer): a blue wash over every 3D scene with atmosphere.
- [Null vertex buffers bound past stream 15](../../../docs/reference/rendering.md#null-vertex-buffers-bound-past-stream-15).
- [Textures in sparse pools taken for attachments](../../../docs/reference/rendering.md#textures-in-sparse-pools-taken-for-attachments): 84 extra render passes a frame.
- [Lazy clears folded into the next pass](../../../docs/reference/rendering.md#lazy-clears-folded-into-the-next-pass): clears of other targets and whole-texture clears.
- [Render targets flagged as images lose lossless compression](../../../docs/reference/rendering.md#render-targets-flagged-as-images-lose-lossless-compression): 53 to 60 fps in a match.
- [A feedback snapshot reused by the next pass with no barrier between](../../../docs/reference/rendering.md#a-feedback-snapshot-reused-by-the-next-pass-with-no-barrier-between): the dark Adventure town.
- [A scaled copy of a Depth16 texture](../../../docs/reference/rendering.md#a-scaled-copy-of-a-depth16-texture).
- [A pool made resident with a page write-protected](../../../docs/reference/memory.md#a-pool-made-resident-with-a-page-write-protected): a GPU reset at the menu video.
- [A command list called after its lent segment went back to the pool](../../../docs/reference/nvn-objects.md#a-command-list-called-after-its-lent-segment-went-back-to-the-pool): a memory runaway, then a crash, once a match had changed the save.
