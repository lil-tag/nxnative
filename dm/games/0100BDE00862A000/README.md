# Mario Tennis Aces (`0100BDE00862A000`)

Update 3.1.0 (v786432), SDK 4.5.0, NVN. Camelot's engine. Modules `main` (build `32487e7e…`), `subsdk0` (the SDK's movie library) and `sdk`. The RomFS keeps its files in Nitro archives (`.narc`, `.ntex`, `.nptl`, `.bin`).

On the Mac (2026-11-01, built from the NSPs with `nxplay --build-only`): the opening movie (it cannot be skipped on a first boot), Adventure mode's first scenes and the island map, the main menu, and a Free Play singles match in Marina Stadium (Mario against Peach, serve and rally). 21,581 of 21,703 shaders build; the 122 that fail are below. Frame rate not measured alone: the run above shared the Mac with another game's shader build (frames of 65–85 ms).

## What it needs

| Setting | Where | Why |
|---|---|---|
| Shaders in Nitro archives | `dm/gfx/tools/shader_split.py` (`narc_members`, `camelot_member`, `camelot_lz`) | see [Shaders in Camelot's Nitro archives](../../../docs/reference/shaders.md#shaders-in-camelots-nitro-archives) |
| `nvnCommandBufferCopyCommands` copies the records | `dm/gfx/nvn/hle_nvn.c` | the game records commands once and copies them into each frame's buffer; see [`CopyCommands` copies the records](../../../docs/reference/nvn-objects.md) |
| An sRGB and a linear texture over the same storage share one Metal texture | `dm/gfx/nvn/nvn_mtl.m` (`srgb_twins`) | the composite and anti-aliasing passes write one format and read the other; see [Textures of an sRGB / linear pair over the same storage](../../../docs/reference/rendering.md#textures-of-an-srgb--linear-pair-over-the-same-storage) |
| `movie::Decoder` base-object constructors (`C2`) | `dm/host/hle_movie.c` | the movie library constructs its decoder through the base-object constructor; see [movies](../../../docs/reference/movies.md) |
| Compact shared font | `dm/host` (PR #797) | the game's font loading needs the compact stand-in |
| Memory: the 4 GB layout, `sys_resource 0x0` | [`game.tsv`](game.tsv) | the NPDM declares no system resource, as for Dark Souls Remastered |

## Found on this game

- Camelot's NARC members say how they are packed in their first byte (0x10/0x20 zlib, 0x30 raw deflate, 0x50 Camelot LZ, 0x70/0x80 stored after 16 bytes, 0x94 LZ4 block). `main+0x20c3a4` in 3.1.0 is the dispatch, `main+0x20b4d8` the LZ decoder.
- Earlier bring-up on 3.1.1 by sen-6ei (issue 1 of `sen-6ei/nxnative`) listed seven fixes; five are the rows above or were already in the tree. The viewport-mask one was left out (next section).

## Open

- **Lava is not drawn** (Adventure's island map; most likely the Bowser's Castle court too): its program has tessellation stages and a geometry shader (vertex `a35ef1d624ddee49`, geometry `01405a80a24425e8`, `/shader/lava/lava.bnsh`), and the Mac layer draws neither tessellation ([Tessellation stages are not drawn](../../../docs/reference/shaders.md#tessellation-stages-are-not-drawn)) nor this combination. The draw is skipped with a red line (the first 8 times).
- **122 shader variants do not compile**: vertex shaders of `/ssg_system/shadowmap/out/shadowmap.bfsha` (and others) that write the viewport mask (`gl_BuiltIn_5253`), which Metal has no output for. The game asked for none of them in the menus, Adventure's first scenes, or matches on grass and clay (it would print `[RED] shader … failed to load`); the players' shadows draw. Supporting them changes the shader chain.
- Distant crowd flicker was reported on 3.1.1; not checked here.
- Frame rate not measured with the Mac idle. No smoke route yet (`smoke.tsv`).
