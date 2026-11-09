# Online play guide

This guide covers the online and LAN play on the `nextendo`, `mario-kart-world-2` and `fever` branches. The Ocarina branches don't have it, since that game is single-player.

[Back to the README](../README.md)

## What it is

Nintendo's online servers only accept real consoles. [Nextendo Network](https://nextendo.network) is a community-run service with its own servers for Switch games, and its own accounts. Online play here uses no Nintendo account: the game's requests for Nintendo's servers go to Nextendo.

nxnative doesn't emulate a Switch: the game's code runs on the Mac, and nxnative answers the calls the game would make to the Switch's system. Upstream answers every online call with "offline". This fork answers them for real when online play is on:

- **The network:** the game uses the Mac's own internet connection and sockets.
- **Server names:** when the game looks up one of Nintendo's server names, the fork answers with Nextendo's address. Every other name is looked up normally.
- **Secure connections:** the game's encrypted connections run through macOS, and go only to Nextendo's servers and to the game servers Nextendo hands out.
- **Sign-in:** the fork signs the game in with your Nextendo account number and game token, the way the Switch signs in with a Nintendo account.
- **Network test:** games run a test of your connection (the NAT check) against Nextendo's test server, as they would against Nintendo's.

Online play is off unless you turn it on. Without it the game behaves exactly as upstream's.

## Set it up

1. **Make a Nextendo account** on [nextendo.network](https://nextendo.network). For the signed-in account, `https://nextendo.network/api/nex-token` answers with the three things you need: your game token (`nex_token`), your account number (`pid`) and your user name (`username`). A token lasts 30 days, then you need a new one.
2. **Save your details** in `~/Library/Application Support/nxnative/nextendo.env`, a plain text file:

   ```
   DM_NEXTENDO_PID=<your account number>
   DM_NEXTENDO_TOKEN=<your game token>
   DM_NEXTENDO_NAME=<your user name>
   DM_NEXTENDO_COUNTRY=<two letters, such as CA or MX>
   ```

   Then make it readable only by you:

   ```
   chmod 600 ~/Library/Application\ Support/nxnative/nextendo.env
   ```

3. **Turn online play on for the game:** tick "Nextendo online play" in the settings window the game's Mac app shows before it starts, or in your launcher. Without a window, add the line `DM_NEXTENDO=1` to `~/Library/Application Support/nxnative/<title ID>/env.txt` (the [README](../README.md#everything-this-fork-adds) explains title IDs).

The file holds your token. Don't share it, and don't commit it to a repo. Every game reads the same file, so you set it up once.

This fork has no sign-in window. A launcher can add one, with its own client ID from Nextendo, and then write this file for you. How is in [docs/frontends.md](https://github.com/lil-tag/nxnative/blob/nextendo/docs/frontends.md#online-play) on the `nextendo` branch.

### Settings for online play

| Setting | What it does |
|---|---|
| `DM_NEXTENDO=1` | Turns online play on. |
| `DM_NEXTENDO_PID`, `DM_NEXTENDO_TOKEN`, `DM_NEXTENDO_NAME`, `DM_NEXTENDO_COUNTRY` | Your account details. Set as variables, they win over the sign-in file. The country is the flag Mario Kart shows online. |
| `DM_NEXTENDO_ACCOUNT=<file>` | Reads another sign-in file instead of `nextendo.env`. |
| `DM_NICK=<name>` | The name the game shows for you. Online it defaults to your Nextendo name. |
| `DM_LAN=1` | LAN play instead: the game's own LAN mode, on your home network only, with no account. If online play is also on, online play wins. |

## Game by game

| Game | Version | Furthest point reached | Not done yet |
|---|---|---|---|
| Super Smash Bros. Ultimate | | Online matches in Arena and Quickplay | Two matches dropped with error 2618-0513 |
| Pokémon Scarlet | 4.0.0 only | Signs in, stays online, receives Nextendo's event data | No battle played. One connection still fails with 2321-4992 |
| Mario Tennis Aces | | Matchmaking | No match played, as nobody else was on the server |
| Super Mario Party Jamboree | 2.3.0 only | The match search | No match played, as nobody else was searching |
| Mario Kart World | 1.0.0 | Not tested online. LAN play opens a room and answers searches | Nobody has joined a LAN room yet |
| Mario Tennis Fever | | Not tested online | |

"Only" means the game needs a patch made for that exact version, so other versions won't get online.

No other game has been tested online on this fork.

### For developers: what each game needed

You don't need this section to play. A game that fails online usually stops at one system call that upstream still answers with "offline" or "not supported". Most of this work was finding that call and answering it the way a Switch would. Here is what each game needed, for anyone adding the next one.

**Super Smash Bros. Ultimate**

- The rest of the Switch's secure-connection calls, which the game's web library uses to keep its connections to Nextendo open.
- Network addresses laid out in the order the game expects, so it reaches matchmaking.
- The play history calls, which upstream refused. Smash asks for a key to record the players it met before joining them, and gave up with "communication error" without it.
- The Switch's command-memory size answered correctly. The lobby's sort crashed when the size drifted.
- A crash after the title screen fixed. Once Nextendo's news was downloaded, the game copied each news picture through a small buffer it sets aside for graphics commands, and the Mac's version of that copy command was too big for it.

**Super Mario Party Jamboree 2.3.0**

Jamboree uses a different online system from Smash and Mario Kart, built on gRPC, Google's network library. It needed:

- A Switch event object that gRPC starts with, plus two more socket calls.
- Socket errors reported where gRPC reads them.
- The network clock and proxy settings, which it checks before going online.
- The root certificates from your firmware, which it uses to check servers.
- Nextendo's published patch for this exact game version. Jamboree checks the server's certificate in its own code, so it refuses Nextendo's servers without it. The patch is in `dm/games/0100965017338000/patches.py`, and other versions of the game won't get online.

**Mario Tennis Aces**

- The answer to the game's "network request finished" check, without which its menu just returned to itself.
- Older forms of four socket calls that this game's SDK uses.
- Trust for the addresses Nextendo hands out for game servers.
- The friends list and notification calls, which must succeed online.

**Pokémon Scarlet 4.0.0**

- The online membership check, which crashed.
- A system object the game deletes in its own way, built with the layout the game expects.
- Nextendo's published certificate patch for this version, in the game's `patches.py`.
- **Event downloads:** the fork fetches Nextendo's event package for the game from `https://nextendo.network/api/bcat/<title ID>`, keeps it in `~/Library/Application Support/nxnative/bcat/<title ID>.zip`, and serves its files to the game. Without it the game shows error 2122-0008 or 2122-0001 soon after going online.
- Two folders Nextendo doesn't publish (`stadium` and `encountOutbreak`) are shown as empty, as Nextendo's own Ryujinx build does, so the game stays online.
- **Terms pages:** the Battle Stadium opens a web page of terms first. The fork answers it as accepted, as Nextendo's Ryujinx build does. Other web pages stay closed, and their address is written to the log. No battle has been tried after it yet.

### Certificate patches

Jamboree 2.3.0 and Scarlet 4.0.0 check the server's certificate in their own code, against Nintendo's. Nextendo publishes a small patch for each supported game version that switches that check off. This fork applies the same patch, from each game's `patches.py`. It applies only to that game version, and only changes the certificate check.

## When a game doesn't connect

1. **Check the token.** It lasts 30 days.
2. **Check the error code** on the game's error screen. The fork also writes it to the log.
3. **Turn on the trace:** `DM_NEXTENDO_TRACE=1` logs every network and secure-connection call with its result. A call that fails right before the error is usually the cause.
4. **Look for an "offline" answer.** If the game calls something that nxnative still answers with "offline" or "not supported", that call is the likely gap. Nextendo's [Ryujinx build](https://github.com/NextendoNetwork/Ryujinx-Nextendo) is the reference for how each call should answer.

Other settings for this work:

| Setting | What it does |
|---|---|
| `DM_NEXTENDO_TRACE=1` | Logs every socket and secure-connection call, its result and error. |
| `DM_NEXTENDO_DUMP=<file>` | Writes the decrypted content of every secure connection to a file. It holds your session data, so use it only for debugging and never share the file. |
| `DM_NEXTENDO_SERVER`, `DM_NEXTENDO_NAT` | Point Nintendo's server names and the network test at another server, for a test server. |

## Known errors

| Error | What it was |
|---|---|
| 2618-0513 in a Smash arena | Another player in the arena advertised a VPN address that never answered. Their setup, not this fork. |
| 2122-0008 or 2122-0001 in Pokémon Scarlet | The event download was missing. Fixed. |
| 2321-4992 | The game couldn't check a server's certificate. Root certificates are now served from your firmware. One connection in Scarlet still shows it. |
