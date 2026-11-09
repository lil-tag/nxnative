# nxnative with online play and Ocarina of Time on the Mac

Play your own Switch and Switch 2 games on a Mac with Apple silicon, without an emulator. Some of them can also go online through [Nextendo Network](https://nextendo.network), a community-run online service for Switch games.

This is a fork of [nxnative](https://github.com/seedds/nxnative) by seedds, called "upstream" below: the original project this one copies and adds to. An emulator such as Ryujinx imitates the whole console while the game runs. nxnative instead turns the game's own code into a Mac app ahead of time, so it runs directly on the Apple chip, and it replaces only the Switch's system services and graphics with Mac versions (graphics on Metal, Apple's graphics system). Upstream does the hard part. This fork adds:

- **Online play** through Nextendo, and **LAN play** on your home network. So far Super Smash Bros. Ultimate is the only game that has played full online matches.
- **Your Ryujinx saves and Miis**, brought over with one command.
- **The Legend of Zelda: Ocarina of Time** (the Switch 2 remake), tuned on an M4 Mac mini: first-visit freezes under 1 second instead of up to 16, later launches ready in about 3 seconds instead of 6 minutes, picture faults such as a white cloud fixed, and a Game menu to change frame rate and resolution while you play.
- **Fixes for Mario Kart World, Mario Tennis Fever, Mario Tennis Aces and Pokémon Scarlet**, including LAN play in Mario Kart World and 60 fps matches in Fever. Each one is listed under [Games](#games).

Everything here is tested on one Mac: an M4 Mac mini (the base M4 chip) with 24 GB of memory, on macOS 26. iPhone and iPad are untested.

Two guides go deeper than this page:

- [Online play guide](fork/online-play.md): how online play works, how to set it up, and how far each game gets.
- [Ocarina of Time guide](fork/ocarina-of-time.md): every change for that game, its settings, how fast it runs and what's still wrong.

This page was last updated on 9 November 2026.

## Pick a branch

Each game sits on its own branch, because each needed a different version of upstream to start from. The branches are not merged with each other.

| Branch | Use it for | What's in it | Online play |
|---|---|---|---|
| `ocarina-7` | The Legend of Zelda: Ocarina of Time | The build I play now. Upstream from 8 November 2026, plus the changes in the [Ocarina guide](fork/ocarina-of-time.md). | No, the game is single-player |
| `ocarina-8` | Ocarina of Time, next version | `ocarina-7` plus HDR output and a lighting fix. Still being tested, and a 40 fps cap is being added. | No |
| `nextendo` | Online play, saves, Miis, and the other games below | Upstream from 3 November 2026, plus this fork's online play, save and Mii features. | Yes |
| `mario-kart-world-2` | Mario Kart World 1.0.0 | The `nextendo` branch on upstream from 4 November 2026, plus LAN play and Mario Kart fixes. | Yes, and LAN |
| `fever` | Mario Tennis Fever | The `nextendo` branch on upstream from 4 November 2026, plus Fever fixes. | Yes |
| `ocarina-2` | Nothing new | An older Ocarina of Time branch, kept for its history. Use `ocarina-7`. | No |
| `main` | Reading these pages | Upstream's latest code and these pages, with none of the fork's changes. | No |

For a game that isn't in this table, start with [upstream](https://github.com/seedds/nxnative). It changes every day and may already support it better than an older branch here.

## Everything this fork adds

Nothing changes unless you turn it on, except rows marked "Always on".

**How to turn a setting on.** Settings are written as `DM_NAME=value`. There are two ways to set them for a game you open from Finder:

- **The settings window.** A Mac app built by this project shows its settings in a window before the game starts. Settings with a name in quotes in the tables below are there.
- **A text file** for everything else: `~/Library/Application Support/nxnative/<title ID>/env.txt`, one `DM_NAME=value` per line. The title ID is the game's 16-character code, which names its folder in `dm/games`. Ocarina of Time's is `0400486024DEC000`. A line in this file wins over the window.

A launcher such as NX Play or NX2Apple shows the same settings in its own window.

### Online and local play

On `nextendo`, `mario-kart-world-2` and `fever`.

| Feature | What it does | How to turn it on |
|---|---|---|
| Nextendo online play | Sends the game to Nextendo's servers instead of Nintendo's. See the [online play guide](fork/online-play.md). | `DM_NEXTENDO=1` and a sign-in file. Setting: "Nextendo online play" |
| Event downloads | Online, fetches the game's event data from Nextendo, the way a Switch downloads news and events. Nextendo has data for Pokémon Scarlet, Pokémon Legends: Z-A and Super Smash Bros. Ultimate. | Always on when online |
| Pokémon terms pages | Pokémon battle modes open a web page of terms before you can play. The fork answers it as accepted, as Nextendo's own Ryujinx build does, so the battle modes open. | Always on when online |
| LAN play | Runs Mario Kart World's own LAN mode on your home network. No account needed. | `DM_LAN=1`. Setting: "LAN play" |
| Your nickname | The name the game shows for you: your Nextendo name when online, otherwise "Player". | `DM_NICK=<name>` |

### Saves, Miis and system data

On `nextendo`, `mario-kart-world-2` and `fever`.

| Feature | What it does | How to use it |
|---|---|---|
| Ryujinx saves | Copies a game's save from Ryujinx, starts an empty save, or switches back to a save you kept. It keeps whatever it replaces, so nothing is lost. | `python3 scripts/ryujinx_save.py import <game folder>` |
| Your Miis | Games list the Miis from your Ryujinx Mii database before the six default ones. | Always on. `DM_MII_DATABASE=<file>` picks another database |
| System data from your firmware | Mii heads, and the certificates that online games check, come from your own console firmware. The build script copies them from Ryujinx's firmware if it finds it. | Automatic at build time, or `nx_extract --sysdata` by hand |

### Picture, speed and the Mac window

| Feature | Branches | What it does | How to turn it on |
|---|---|---|---|
| Higher resolution | `nextendo` and the branches built on it | Draws a game at 1.5× or 2× (2× is 4K for a 1080p game). Costs a lot of GPU time. | `DM_RES_SCALE=1.5` or `2` |
| Faster drawing in Tears of the Kingdom | `nextendo` and the branches built on it | Speeds up transform feedback, a GPU feature that The Legend of Zelda: Tears of the Kingdom uses a lot. At 2× it then draws real 4K. | Always on |
| Play in the background | All except `ocarina-2` | Keeps the game running, and your controller playing, while another app is in front. It keeps using power. | `DM_BACKGROUND_PLAY=1`. Setting: "Play in the background" |
| Game menu | `ocarina-7`, `ocarina-8` | A Game menu in the game window's menu bar changes frame rate, "Hold frames when late", render scale, dynamic resolution and console mode while you play. Your choice is kept for next time. | The Game menu |
| Frame-rate counter | `ocarina-7`, `ocarina-8` | A small counter in the top left with frames per second and the time per frame. | `DM_SHOW_FPS=1` |
| Metal performance overlay | `ocarina-7`, `ocarina-8` | Apple's overlay with frame rate, GPU time and memory. | Window > Metal HUD (⌘⇧H), or `DM_METAL_HUD=1` |
| Microphone | `ocarina-7`, `ocarina-8` | The Mac's microphone becomes the game's microphone. In Ocarina of Time you can play the ocarina into it. macOS asks for permission the first time. | `DM_MIC=1` |
| HDR output | `ocarina-8` | Sends the game's HDR picture to an HDR screen. New and still being tested. | `DM_HDR=1`. Setting: "HDR output" |

Ocarina of Time has more settings of its own. They're listed in the [Ocarina guide](fork/ocarina-of-time.md#settings).

### Games

| Game | Branch | What this fork adds or fixes |
|---|---|---|
| The Legend of Zelda: Ocarina of Time (Switch 2, version 1.1.1) | `ocarina-7` | Fewer freezes the first time you meet an enemy or enter an area, later launches in seconds instead of minutes, movies at full speed, a white cloud and other picture faults fixed, a memory guard that stops the game before it can freeze the Mac, and settings for frame rate and resolution. Full list in the [Ocarina guide](fork/ocarina-of-time.md). |
| Mario Kart World 1.0.0 | `mario-kart-world-2` | LAN play, with Wireless Play opening the LAN rooms. Uses less memory: texture pieces that share memory on the Switch now share it on the Mac too, instead of being stored twice. A movie no longer crashes when the game's video thread runs late, and a GPU reset is fixed. |
| Mario Tennis Fever | `fever` | 60 fps in matches on an M4. The dark town in Adventure mode is lit again. The crash and GPU reset after a finished match are fixed. |
| Mario Tennis Aces | `nextendo` | A game profile, which upstream doesn't have: its shaders, its texture copies and its movie decoder. Online it reaches matchmaking. |
| Pokémon Scarlet 4.0.0 | `nextendo` | Plays on the Mac. Online it signs in, stays online and receives Nextendo's event data. |
| Super Smash Bros. Ultimate | `nextendo` | Online matches in Arena and Quickplay. A crash after the title screen, caused by the online news pictures, is fixed. |
| Super Mario Party Jamboree 2.3.0 | `nextendo` | Online it reaches the match search. |

## Online play through Nextendo

Nintendo's online servers only accept real consoles. [Nextendo Network](https://nextendo.network) runs its own servers for Switch games. With online play on, this fork sends the game's requests for Nintendo's servers to Nextendo instead, and signs in with your Nextendo account. It uses no Nintendo account.

You need a Nextendo account, and your account number and game token saved once in a small file. The [online play guide](fork/online-play.md) has the three setup steps, what each game can do online, and what to check when a game doesn't connect.

How far each game gets, in short:

| Game | Furthest point reached |
|---|---|
| Super Smash Bros. Ultimate | Online matches in Arena and Quickplay |
| Pokémon Scarlet 4.0.0 | Signed in and online, with Nextendo's event data. No battle played yet |
| Mario Tennis Aces | Matchmaking. Nobody else was on the server to play |
| Super Mario Party Jamboree 2.3.0 | The match search. Nobody else was searching |
| Mario Kart World 1.0.0 | Not tested online. LAN play opens a room and answers searches; nobody has joined yet |

## Ocarina of Time

This is the game I worked on most. On the M4 Mac mini it runs at about 37 to 43 frames per second while walking from Kokiri Forest to Hyrule Field, drawn at 1536×864 and upscaled to the screen, with "Hold frames when late" off. That setting is on by default and costs a few frames per second (27 against 31 on an earlier branch). Upstream reports a locked 60 fps in Kokiri Forest on an M6, a newer and faster chip. I haven't timed upstream's own code on my Mac.

The `ocarina-7` branch is upstream's code from 8 November 2026, plus:

- open upstream pull requests that help this game, many of them through [swordboiMacNative](https://github.com/Fasen24-AI/swordboiMacNative), another fork, tested on an M5 Pro
- this fork's own fixes, found while playing it on the M4.

Before these changes the game froze for up to 16 seconds the first time you entered an area, spent 6 minutes preparing shaders on most launches, and played its movies at half speed. Now the worst first-visit freeze I measured on the same walk is under 1 second, a launch after the first prepares in about 3 seconds, and movies run at full speed.

The [Ocarina guide](fork/ocarina-of-time.md) explains each change in plain words, lists the settings, gives the measurements, and lists what's still wrong and what's being worked on.

## Frame rates on the M4 Mac mini

Each line is one or two sessions on my Mac, not a controlled comparison.

| Game | Where | Frames per second |
|---|---|---|
| Ocarina of Time (`ocarina-7`) | Walking from Kokiri Forest to Hyrule Field, "Hold frames when late" off | 37 to 43 (two runs) |
| Mario Tennis Fever | A match, on a quiet Mac at night | 60 |
| Mario Tennis Fever | A match, while the Mac was in daytime use | 53 to 59 |
| Mario Tennis Fever | Adventure mode's town | 40 to 45 |
| Mario Kart World 1.0.0 | 13 minutes of Grand Prix and Knockout Tour | About 52 on average |
| Mario Kart World 1.0.0 | Two players, split screen | 43 to 45 |
| Super Smash Bros. Ultimate | At 2× resolution | 60 |
| Pokémon Legends: Z-A | At 2× resolution | Mostly 30 |

## Build a game

You need macOS 26.6 or later, Xcode 27.2, [Homebrew](https://brew.sh) and an arm64 Python 3.12 or newer.

You also need the game's files, made from your own console and your own copy of the game. For Switch games you also need `prod.keys`, the decryption keys from your own Switch. This page doesn't cover making them.

**1. Install the tools**, once:

```
sudo xcode-select -s /Applications/Xcode.app
xcodebuild -downloadComponent MetalToolchain
brew install xcodegen zstd glslang
brew install --cask dotnet-sdk
python3 -m pip install lz4 "capstone>=5,<6" fonttools nsz zbic "backports.zstd; python_version < '3.14'"
```

**2. Get the branch for your game**, from the table at the top:

```
git clone https://github.com/lil-tag/nxnative.git
cd nxnative
git checkout ocarina-7
```

For games on different branches, give each branch its own folder instead of switching back and forth:

```
git worktree add ../nxnative-mario-kart mario-kart-world-2
```

**3. Build.** You get a Mac app, `<game name>.app`, in the same folder as the game's files. Open it from Finder; you don't need a launcher.

For a Switch game, put its `.nsp`, `.xci`, `.nsz` or `.xcz` files in one folder, with any update and DLC:

```
python3 scripts/build_ipa_from_nsp.py ~/Games/MyGame -k ~/keys/prod.keys --platform mac-app
```

For a Switch 2 game such as Ocarina of Time, use its `.nup` file (one file holding the game, its updates and DLC) or a folder of its files. You don't need `prod.keys`. It only works for a game with a profile: a folder in `dm/games`, named by the game's title ID, that holds its settings and fixes. Ocarina of Time has one.

```
python3 scripts/build_ipa_from_nsp.py ~/Games/OcarinaOfTime.nup --platform mac-app
```

Plan for time and disk space. Mario Kart World took 28 minutes on my Mac. Ocarina of Time took 2 hours 20 minutes and about 62 GB the first time.

Upstream's [README](https://github.com/seedds/nxnative#how-to-play) covers iPhone installs and the other ways to build a Switch 2 game.

### Use it with a launcher

You can also start games from a launcher that lets you choose your nxnative folder. [NX Play](https://github.com/varyform/nx-native-wrapper) does, through its `nxnativePath` setting. I now play through my own build of [NX2Apple](https://github.com/LNPANDY/NX2Apple), set to use this fork. Its released app uses its own copy of nxnative, so it doesn't have this fork's changes.

## Known gaps

- **Ocarina of Time:** see [what's still wrong](fork/ocarina-of-time.md#whats-still-wrong) in its guide.
- **LAN play:** Mario Kart World opens a room and answers searches. Nobody has joined one or raced yet, because that needs a second machine.
- **Online:** two Smash matches dropped with error 2618-0513. In one of them the other player's network was the cause.
- **Branches fall behind upstream.** Upstream changes daily. Each branch here is a snapshot that worked on my Mac.

## Credits and legal

This repo holds no games, keys, saves or firmware. You need your own copy of each game. This project isn't affiliated with Nintendo or with Nextendo Network.

The license is MIT, as upstream's is. This work stands on:

- **[nxnative](https://github.com/seedds/nxnative)** by seedds and its contributors, and [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile), where the approach began.
- **Ocarina of Time:** [swordboiMacNative](https://github.com/Fasen24-AI/swordboiMacNative) (Fasen24-AI, commits by EmmanueleC), the people behind upstream's Ocarina port and its pull requests (listed in the [Ocarina guide](fork/ocarina-of-time.md#credits)), and the forks I compared against: Aznracer's [Mighty-Green-Kokiri-Kid](https://github.com/Aznracer/Mighty-Green-Kokiri-Kid) and cheemu21's [kid-with-sword-remake](https://github.com/cheemu21/kid-with-sword-remake).
- **Mario Kart World:** [kartnative](https://github.com/pahorema/kartnative), for its memory settings and tile sharing idea.
- **Online play:** [Ryujinx-Nextendo](https://github.com/NextendoNetwork/Ryujinx-Nextendo), whose behaviour this fork follows, and Nextendo Network for its servers.
- **Third-party code:** Ryujinx's shader translator, SPIRV-Cross, glslang, LibHac and others, with licenses in [dm/third_party](dm/third_party/README.md).
