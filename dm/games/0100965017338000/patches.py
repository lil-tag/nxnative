# Super Mario Party Jamboree (0100965017338000): the rewriter's patches of this game's own builds. dm/tools/patch_nso.py loads every profile's patches.py and applies those of a
# module's NSO build ID; CUTS, OBS and FRAME_STEP as described there ("Registered patches"). Patches of Nintendo SDK module builds are in dm/tools/sdk_patches.py
CUTS = {
  # 2.3.0 (v458752) main: the online library's (npln) gRPC checks the server's certificate itself, against the firmware's Nintendo roots (BoringSSL inside main, not nn::ssl),
  # so Nextendo's own certificate is refused: it connects to Nextendo, reads the reply and hangs up (error 2321-4992). At main+0xad38bc it loads its "skip the server certificate
  # check" option (gRPC's skip_server_certificate_verification) and picks the empty verify callback when it is set (main+0xad38d8, SSL_CTX_set_verify). This is Nextendo's own
  # patch for this build, "mpj-230-certificate" in its signed NPLN patch catalog (nextendo.network/service-catalog/v1/npln-patches/0100965017338000/<build ID>, which
  # Ryujinx-Nextendo and Citron apply): the same guard and bytes. Offline nothing connects, so it changes nothing there
  'd70ce1726c979e45ed4200e9c2b23b2a': [
    (0xad38bc, 'aae24039', '2a008052', 'gRPC server certificate check: ldrb w10, [x21, #0x38] → mov w10, #1 (skip it; Nextendo signs its own)'),
  ],
}

# How general each patch is and could be (dm/tools/docs_patches.py "Binding", listed in docs/agents/game-patches.md): what, its binding now, the loosest it could have (goal),
# purpose, why; blocked or kept: why the gap stays open (cannot close yet, or left open on purpose); checked: what was checked on game files (else a judgement)
BINDING = [
    {'what': "gRPC's server certificate check skipped", 'now': 'build', 'goal': 'build', 'purpose': 'runtime',
     'why': "online play on Nextendo; Nextendo's catalog publishes it per build, so a new game build gets its own entry there",
     'checked': 'the original instruction at main+0xad38bc of 2.3.0 is the guard in the catalog, and the code around it is the verify-callback choice'},
]
