# Pokémon Scarlet (0100A3D008C5C000): the rewriter's patches of this game's own builds. dm/tools/patch_nso.py loads every profile's patches.py and applies those of a
# module's NSO build ID; CUTS, OBS and FRAME_STEP as described there ("Registered patches"). Patches of Nintendo SDK module builds are in dm/tools/sdk_patches.py
CUTS = {
  # 4.0.0 (v786432) main: the online library (npln) does TLS itself (gRPC over BoringSSL in main) and checks the server against the firmware's Nintendo roots, so Nextendo's
  # own certificate would be refused. Nextendo's patch for this build, "pokemon-scarlet-npln-421c5411" in its signed NPLN patch catalog
  # (nextendo.network/service-catalog/v1/npln-patches/0100a3d008c5c000/<build ID>, which Ryujinx-Nextendo and Citron apply; marked not final there): the same guards and
  # bytes. The first two drop error branches in the loop that loads the root certificates, the third skips the server certificate check (as Super Mario Party Jamboree's).
  # Offline nothing connects, so they change nothing there
  '421c5411b487eb4d049dd065fec95477': [
    (0x1fe4b4, 'a0060035', '1f2003d5', 'root certificate loading: cbnz w0, main+0x1fe588 → nop (an entry that fails to load is skipped)'),
    (0x1fe558, '61020054', '1f2003d5', 'root certificate loading: b.ne main+0x1fe5a4 → nop (the count check)'),
    (0x1feea0, 'aae24039', '2a008052', 'gRPC server certificate check: ldrb w10, [x21, #0x38] → mov w10, #1 (skip it; Nextendo signs its own)'),
  ],
}

# How general each patch is and could be (dm/tools/docs_patches.py "Binding", listed in docs/agents/game-patches.md): what, its binding now, the loosest it could have (goal),
# purpose, why; blocked or kept: why the gap stays open (cannot close yet, or left open on purpose); checked: what was checked on game files (else a judgement)
BINDING = [
    {'what': "gRPC's root certificate loading and server certificate check relaxed", 'now': 'build', 'goal': 'build', 'purpose': 'runtime',
     'why': "online play on Nextendo; Nextendo's catalog publishes it per build, so a new game build gets its own entry there",
     'checked': 'the three original instructions of 4.0.0 are the guards in the catalog; the third is the verify-callback choice, as in Super Mario Party Jamboree 2.3.0'},
]
