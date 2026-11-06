# nxnative with Nextendo online play

Play your own Switch and Switch 2 games on a Mac without an emulator, and take some of them online through [Nextendo Network](https://nextendo.network).

This is a fork of [nxnative](https://github.com/seedds/nxnative). The game's code runs directly on the Apple chip, with its graphics rebuilt on Metal. This fork adds online play, LAN play, Ryujinx save import and a sharper resolution setting, and it tunes a few games for a plain M4 Mac.

I test on one Mac only: an M4 Mac mini with 24 GB of memory. iPhone and iPad are untested for everything here.

## Pick a branch

The work is in four branches. They are not merged with each other, and all four are behind upstream's `main`. The `main` branch itself is upstream's code plus this page, so it has none of these features.

| Branch | Use it for | Includes Nextendo code |
|---|---|---|
| `nextendo` | Online play and the features below | Yes |
| `mario-kart-world-2` | Mario Kart World 1.0.0, with LAN play | Yes |
| `fever` | Mario Tennis Fever | Yes |
| `ocarina-2` | The Legend of Zelda: Ocarina of Time (Switch 2) | No |

## What this fork adds

| Feature | What it does | How to use it |
|---|---|---|
| Nextendo online play | Takes games online through Nextendo. Off unless you turn it on. | `DM_NEXTENDO=1`, plus the sign-in file below |
| LAN play | Runs a game's own LAN mode on your local network, with no account. On `mario-kart-world-2` only. | `DM_LAN=1` |
| Ryujinx saves | Copies a game's save from Ryujinx, starts an empty save, or switches to a kept one. It keeps whatever it replaces. | `python3 scripts/ryujinx_save.py import <game folder>` |
| Your Miis | Games list your Ryujinx Miis before the six default ones. Mii heads also need the MiiModel archive from your own firmware. | Automatic. For heads: `nx_extract --sysdata` |
| Sharper resolution | Draws games at 1.5× or 2× (4K). It costs a lot of GPU time. | `DM_RES_SCALE=1.5` or `2` |
| Play in the background | Keeps the game and your controller running while another app is in front. | `DM_BACKGROUND_PLAY=1` |
| Mario Tennis Aces | Adds a game profile that upstream doesn't have. | Build it like any other game |

A launcher that draws nxnative's settings list shows online play, LAN play, resolution and background play as settings.

## Set up Nextendo online play

Nextendo Network is a separate online service for Switch games. With online play on, this fork sends Nintendo's server names to Nextendo's servers. It makes encrypted connections only to Nextendo's addresses.

1. Get a Nextendo account, and your game token from Nextendo's `/api/nex-token`. A token lasts 30 days.
2. Save your details in `~/Library/Application Support/nxnative/nextendo.env`, readable only by you:

   ```
   DM_NEXTENDO_PID=<your account number>
   DM_NEXTENDO_TOKEN=<your game token>
   DM_NEXTENDO_NAME=<your user name>
   DM_NEXTENDO_COUNTRY=<two letters>
   ```

3. Start the game with `DM_NEXTENDO=1`.

This fork has no sign-in window. A launcher can add one with its own client ID from Nextendo, then [write this file for you](https://github.com/lil-tag/nxnative/blob/nextendo/docs/frontends.md#online-play). The file holds your token, so don't share it or commit it.

### How far each game gets online

| Game | Furthest point reached | Not done yet |
|---|---|---|
| Super Smash Bros. Ultimate | Online matches in Arena and Quickplay | Two matches dropped with error 2618-0513 |
| Super Mario Party Jamboree 2.3.0 | The match search | No match played, as nobody else was searching |
| Mario Tennis Aces | Matchmaking | No match played, as nobody else was on the server |
| Pokémon Scarlet 4.0.0 | Signs in and stays online, with Nextendo's event data | No battle played |

No other game has been tested online on this fork, including Mario Kart World and Mario Tennis Fever.

Jamboree and Scarlet check the server's certificate in their own code, so they would refuse Nextendo. Their `patches.py` files skip that check, using the patch Nextendo publishes for that exact game version. Other versions of those games won't get online.

## Frame rates on the M4 Mac mini

These are single sessions on my Mac, not controlled comparisons.

| Game | Where | Frames per second |
|---|---|---|
| Ocarina of Time | Deku Tree rooms | 45 to 59 |
| Ocarina of Time | Porch of Link's house, Kokiri Forest | About 39 |
| Mario Tennis Fever | A match, on a quiet Mac at night | 60 |
| Mario Tennis Fever | A match, on the same Mac in daytime use | 53 to 59 |
| Mario Tennis Fever | Adventure mode's town | 40 to 45 |
| Mario Kart World 1.0.0 | 13 minutes of Grand Prix and Knockout Tour | About 52 on average |
| Mario Kart World 1.0.0 | 2-player split screen | 43 to 45 |
| Super Smash Bros. Ultimate | At 2× resolution | 60 |
| Pokémon Legends: Z-A | At 2× resolution | Mostly 30 |

## Build a game

You need macOS 26.6 or later, Xcode 27.2, [Homebrew](https://brew.sh) and an arm64 Python 3.12 or newer. You also need your own copy of the game.

**1. Install the tools**, once:

```
sudo xcode-select -s /Applications/Xcode.app
xcodebuild -downloadComponent MetalToolchain
brew install xcodegen zstd glslang
brew install --cask dotnet-sdk
python3 -m pip install lz4 "capstone>=5,<6" fonttools nsz zbic "backports.zstd; python_version < '3.14'"
```

**2. Get the branch you picked:**

```
git clone https://github.com/lil-tag/nxnative.git
cd nxnative
git checkout nextendo
```

**3. Build.** You get a Mac app to open from Finder, so you don't need a launcher.

For a Switch game, put its `.nsp`, `.xci`, `.nsz` or `.xcz` files in one folder, with any update and DLC:

```
python3 scripts/build_ipa_from_nsp.py ~/Games/MyGame -k ~/keys/prod.keys --platform mac-app
```

For a Switch 2 game, use its `.nup` file. It needs no `prod.keys`, and it only works for a game that has a profile in `dm/games`:

```
python3 scripts/build_ipa_from_nsp.py ~/Games/MyGame.nup --platform mac-app
```

Plan for time and disk space. A full build took 28 minutes for Mario Kart World on my Mac. Ocarina of Time took 2 hours 21 minutes and about 62 GB.

Upstream's [README](https://github.com/seedds/nxnative#how-to-play) covers iPhone installs and building a Switch 2 game from a folder of files.

### Use it with a launcher

You can also use a launcher that lets you choose your nxnative folder. [NX Play](https://github.com/varyform/nx-native-wrapper) does, through its `nxnativePath` setting. I haven't checked the others.

## Known gaps

- **`ocarina-2`, picture:** ground fog and the Deku Stick's flame are missing from this branch. I fixed them in my own build by correcting compiled shaders, which can't go in this repo because they come from the game's code. The source fix still has to be written. It needs [upstream PR 1292](https://github.com/seedds/nxnative/pull/1292) and a small tolerance in the fog's depth test.
- **`ocarina-2`, later areas:** I added fixes for crashes other players reported at the Ocarina's notes, on leaving the Deku Tree and on entering Hyrule Field. I haven't reached those places yet, so they are unproven here.
- **`ocarina-2` or upstream:** upstream's `main` has had its own port of this game since 2026-11-06, reporting 60 fps on an M6. Try that first. This branch adds a game clock patch that keeps the game's speed right under 60 fps. It also adds a guard that stops the game with an alert before graphics memory freezes the Mac.
- **LAN play:** Mario Kart World opens a room and answers searches. Nobody has joined one or raced yet, because that needs a second machine.

## Credits and legal

This repo holds no games, keys, saves or firmware. You need your own copy of each game. This project isn't affiliated with Nintendo or with Nextendo Network.

The license is MIT, as upstream's is. This work stands on:

- **[nxnative](https://github.com/seedds/nxnative)** by seedds and its contributors, and [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile), where the approach began.
- **Ocarina of Time:** ChiefSZ13, stan69b, aCidcOde, fumeware and lunks, and Dark's Dynamic FPS for the game clock idea.
- **Mario Kart World:** [kartnative](https://github.com/pahorema/kartnative), for its memory settings and tile sharing idea.
- **Online play:** Ryujinx-Nextendo, whose behaviour this fork follows.
- **Third-party code:** Ryujinx's shader translator, SPIRV-Cross, glslang, LibHac and others, with licenses in [dm/third_party](dm/third_party/README.md).
