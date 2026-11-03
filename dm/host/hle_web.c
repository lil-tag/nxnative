// web: the web page applets. The host has no browser; Pokémon terms pages are answered as agreed, the rest are cut
#include "hle.h"
#include <string.h>

// ---------- web: web page applet (online lobby). Unavailable ----------
HLE_FORCE("_ZN2nn3web13ShowLobbyPageEPNS0_20LobbyPageReturnValueERKNS0_16ShowLobbyPageArgE", "online lobby web applet (cut)", Result, h_web_Lobby, void *rv, const void *arg) { (void)rv; (void)arg; LOG("web::ShowLobbyPage (cut)"); return RES_FAIL; }
// Tears of the Kingdom imports the general web page applet too (the game constructs the return value itself, so it stays as constructed).
// ShowWebPageArg and WebPageReturnValue are 0x2000 bytes: a u16 entry count at +0, then from +8 entries {u16 type, u16 size, u32 pad, data} (SDK 13 getters, sdk+0x40b4c8..)
static const uint8_t *web_tlv(const uint8_t *d, unsigned type, unsigned *size) {
    for (unsigned n = *(const uint16_t *)d, off = 8; n-- && off + 8 <= 0x2000; off += 8 + *(const uint16_t *)(d + off + 2)) {
        unsigned sz = *(const uint16_t *)(d + off + 2);
        if (off + 8 + sz > 0x2000) break;
        if (*(const uint16_t *)(d + off) == type) { *size = sz; return d + off + 8; }
    }
    return NULL;
}
static void web_put(uint8_t *d, unsigned *off, unsigned type, const void *v, unsigned size) {
    *(uint16_t *)(d + *off) = (uint16_t)type; *(uint16_t *)(d + *off + 2) = (uint16_t)size; memcpy(d + *off + 8, v, size); *off += 8 + size; (*(uint16_t *)d)++;
}
// Pokémon Scarlet / Violet (/scvi/) and Legends Z-A (/plza/) open a terms page at https://battle-*.pokemon-home.com before Battle Stadium, competitions and Ranked Battles, and wait on
// its callback: answered as the user agreeing, as Ryujinx-Nextendo does (BrowserApplet.cs): exit reason LastUrl (3) with the callback URL, + "/agree" for the terms callbacks
static int web_pokemon_terms(const char *url, const char *cb, char *out, size_t n) {
    if (strncmp(url, "https://battle-", 15)) return 0;
    const char *path = strchr(url + 8, '/');
    if (!path || path - url < 26 || strncmp(path - 17, ".pokemon-home.com", 17) || (strncmp(path, "/scvi/", 6) && strncmp(path, "/plza/", 6))) return 0;
    size_t page = strcspn(url, "?"); const char *slash = url + page; while (slash > path && *slash != '/') slash--;
    if (strlen(cb) != (size_t)(slash - url) + 9 || strncmp(cb, url, slash - url) || strcmp(cb + (slash - url), "/callback")) return 0;   // same page directory only
    size_t k = strlen(cb);
#define ENDS(s) (k >= sizeof s - 1 && !strcmp(cb + k - (sizeof s - 1), s))
    int agree = ENDS("/terms/callback") || ENDS("/scvi/battle-terms/callback") || ENDS("/scvi/competition/callback");
#undef ENDS
    return snprintf(out, n, "%s%s", cb, agree ? "/agree" : "") < (int)n;
}
HLE_FORCE("_ZN2nn3web11ShowWebPageEPNS0_18WebPageReturnValueERKNS0_14ShowWebPageArgE", "web page applet: Pokémon terms pages agreed, the rest cut", Result, h_web_Page, uint8_t *rv, const uint8_t *arg) {
    char url[0xc01] = "", cb[0x401] = "", last[0x420]; unsigned n;
    const uint8_t *v = web_tlv(arg, 1, &n); if (v) snprintf(url, sizeof url, "%.*s", (int)n, (const char *)v);
    v = web_tlv(arg, 3, &n); if (v) snprintf(cb, sizeof cb, "%.*s", (int)n, (const char *)v);
    if (!web_pokemon_terms(url, cb, last, sizeof last)) { LOG("web::ShowWebPage %.200s (cut)", url); return RES_FAIL; }
    uint32_t reason = 3; uint64_t size = strlen(last) + 1; unsigned off = 8;
    memset(rv, 0, 0x2000);
    web_put(rv, &off, 1, &reason, 4); web_put(rv, &off, 2, last, (unsigned)size); web_put(rv, &off, 3, &size, 8);
    LOG("web::ShowWebPage %.200s: terms agreed (%s)", url, last);
    return RES_OK;
}
// Octopath Traveler 0 imports the offline HTML page applet too (pages shipped in the game's HTML document NCA, which the host does not extract)
HLE_FORCE("_ZN2nn3web19ShowOfflineHtmlPageEPNS0_26OfflineHtmlPageReturnValueERKNS0_22ShowOfflineHtmlPageArgE", "offline HTML page applet (cut)", Result, h_web_Offline, void *rv, const void *arg) {
    (void)rv; (void)arg; LOG("web::ShowOfflineHtmlPage (cut)"); return RES_FAIL;
}
