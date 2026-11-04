# Mario Kart World (0400C3F00006E000): the rewriter's patches of this game's own builds. dm/tools/patch_nso.py loads every profile's patches.py and applies those of a module's NSO build ID;
# CUTS as described there ("Registered patches")
CUTS = {
  # 1.0.0 main: Wireless Play opens the LAN rooms. The title menu's handler (main+0xd54108, a jump table on the selected row) stores mode 3 for its two Wireless Play rows (1 and 2 players) when
  # the menu's flag is set and mode 2 otherwise; main+0x22477b4 then starts nn::nifm::SetLocalNetworkMode(true) and sockets for 3, nn::ldn for 2. nn::ldn is cut, so 2 ends in error 2162-0001.
  # Guides change the row to LAN Play by holding L+R and clicking the left stick; on 1.0.0 that does nothing here and the flag has no writer but its clears (main+0xbf1d44, main+0x9b310c), so the
  # row keeps its Wireless Play label. DM_LAN=1 gives LAN mode its sockets; without it IsNetworkAvailable is false and main+0x2247a94 takes the failure path (read from the code, not run)
  '9e244438c3d8ef1c703a6a85ef99c4f0': [
    (0xd5433c, 'e9040034', '1f2003d5', 'Wireless Play, 1 player: cbz w9 → nop, always LAN (mode 3)'),
    (0xd54364, 'e9030034', '1f2003d5', 'Wireless Play, 2 players: cbz w9 → nop, always LAN (mode 3)'),
  ],
}

# How general each patch is and could be (dm/tools/docs_patches.py "Binding", listed in docs/agents/game-patches.md): what, its binding now, the loosest it could have (goal),
# purpose, why; blocked or kept: why the gap stays open (cannot close yet, or left open on purpose); checked: what was checked on game files (else a judgement)
BINDING = [
    {'what': 'Wireless Play opens the LAN rooms', 'now': 'build', 'goal': 'build', 'purpose': 'runtime',
     'why': "the host has no nn::ldn and the game's own switch to LAN does nothing on this build; a host with nn::ldn would drop this",
     'checked': 'the same change as a cheat on 1.0.0 opened the room list, created a room and reached "Waiting for players to join"'},
]
