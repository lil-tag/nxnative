// Online play (hle_online.c): the Nextendo account behind the network service account calls of hle_account.c.
// The sign-in comes from DM_NEXTENDO_PID / _TOKEN / _NAME / _COUNTRY, else from one file every game reads, ~/Library/Application Support/nxnative/nextendo.env
// (DM_NEXTENDO_ACCOUNT names another): KEY=VALUE lines with those names, which a front end writes once after the player signs in (docs/frontends.md, Online play).
// On a Switch the account service fetches the id token from Nintendo's BAAS; here it is made locally, as Mush8 (nextendo.rs) and Ryujinx-Nextendo (ManagerServer.cs) make it:
// Nextendo's servers check the account's own token, carried in the "nnex" claim, not the id token's signature
#include "hle.h"
#include "online.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BAAS "e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com"
static struct { uint64_t pid; char token[1024], name[64], country[3]; } g_acct;

static void put(const char *key, const char *v) {
    if (!strcmp(key, "DM_NEXTENDO_PID")) g_acct.pid = strtoull(v, NULL, 10);
    else if (!strcmp(key, "DM_NEXTENDO_TOKEN")) snprintf(g_acct.token, sizeof g_acct.token, "%s", v);
    else if (!strcmp(key, "DM_NEXTENDO_NAME")) snprintf(g_acct.name, sizeof g_acct.name, "%s", v);
    else if (!strcmp(key, "DM_NEXTENDO_COUNTRY") && strlen(v) == 2) { g_acct.country[0] = v[0] & ~0x20; g_acct.country[1] = v[1] & ~0x20; }   // two capital letters
}
static void load(void) {
    char path[1024]; const char *p = getenv("DM_NEXTENDO_ACCOUNT"), *home = getenv("HOME");
    if (p && *p) snprintf(path, sizeof path, "%s", p);
    else snprintf(path, sizeof path, "%s/Library/Application Support/nxnative/nextendo.env", home ? home : "");
    FILE *f = fopen(path, "r");
    if (f) {
        char line[1200];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            char *eq = strchr(line, '=');
            if (eq && line[0] != '#') { *eq = 0; put(line, eq + 1); }
        }
        fclose(f);
    }
    static const char *keys[] = {"DM_NEXTENDO_PID", "DM_NEXTENDO_TOKEN", "DM_NEXTENDO_NAME", "DM_NEXTENDO_COUNTRY"};
    for (unsigned i = 0; i < 4; i++) { const char *v = getenv(keys[i]); if (v && *v) put(keys[i], v); }   // the environment wins
    if (g_acct.pid && g_acct.token[0]) LOG("[online] Nextendo account %s (pid %llu, country %s)", g_acct.name[0] ? g_acct.name : "?", (unsigned long long)g_acct.pid, g_acct.country[0] ? g_acct.country : "not set");
    else LOG("[online] not signed in to Nextendo (%s): online play stops at the game's login", path);
}
static void load_once(void) { static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, load); }
int dmo_signed_in(void) { if (!dm_online()) return 0; load_once(); return g_acct.pid && g_acct.token[0]; }
uint64_t dmo_pid(void) { load_once(); return g_acct.pid; }
const char *dmo_name(void) { load_once(); return g_acct.name; }

// ---------- the id token (a JWT): header.claims.signature, base64url ----------
static size_t b64url(char *out, const uint8_t *in, size_t n) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        size_t k = n - i < 3 ? n - i : 3;
        for (size_t j = 0; j <= k; j++) out[o++] = T[v >> (18 - 6 * j) & 63];
    }
    out[o] = 0;
    return o;
}
static void hex(char *out, const uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", b[i]); }
static void json_str(char *out, size_t cap, const char *s) {   // a JSON string, quotes included
    size_t o = 0; out[o++] = '"';
    for (; *s && o + 8 < cap; s++) {
        if (*s == '"' || *s == '\\') { out[o++] = '\\'; out[o++] = *s; }
        else if ((uint8_t)*s < 0x20) o += (size_t)snprintf(out + o, cap - o, "\\u%04x", *s);
        else out[o++] = *s;
    }
    out[o++] = '"'; out[o] = 0;
}
// The installed version (GetDisplayVersion's table); empty for a build the profile does not register, which Ryujinx-Nextendo leaves out too
static const char *display_version(void) {
#ifdef DM_GAME_DISPLAY_VERSIONS
    static const struct { const char *build, *ver; } k[] = {DM_GAME_DISPLAY_VERSIONS};
    for (unsigned j = 0; j < sizeof k / sizeof *k; j++) if (!strcasecmp(dm_main_build(), k[j].build)) return k[j].ver;
#endif
    return "";
}
// Games whose servers route by application as well (Ryujinx-Nextendo: Scarlet shares Violet's tenant, Monster Hunter Rise's JP/HK release the US/EU one).
// shortcut: the NSO classics need app_id too; add their title IDs when one of them is brought up
static int needs_app_id(const char *id) { return !strcasecmp(id, "0100A3D008C5C000") || !strcasecmp(id, "0100559011740000"); }

size_t dmo_id_token(char *out, size_t cap) {
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    static char tok[4096]; static time_t exp; static char sub[33], di[33], did[33];
    load_once();
    pthread_mutex_lock(&m);
    time_t now = time(NULL);
    if (!tok[0] || now + 600 >= exp) {
        uint8_t r[16 * 4 + 256];
        arc4random_buf(r, sizeof r);
        if (!sub[0]) { hex(sub, r, 16); hex(di, r + 16, 16); hex(did, r + 32, 16); }   // one device for the whole run
        char jti[40], nnex[1100], ver[64] = "", app[64] = "", claims[2600];
        const uint8_t *u = r + 48;
        snprintf(jti, sizeof jti, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
        json_str(nnex, sizeof nnex, g_acct.token);
        if (display_version()[0]) snprintf(ver, sizeof ver, ",\"tv\":\"%s\"", display_version());
        if (needs_app_id(DM_GAME_ID)) snprintf(app, sizeof app, ",\"app_id\":\"%s\"", DM_GAME_ID);
        exp = now + 3 * 3600;
        int n = snprintf(claims, sizeof claims,
            "{\"sub\":\"%s\",\"aud\":\"ed9e2f05d286f7b8\",\"iss\":\"https://" BAAS "\",\"iat\":%ld,\"nbf\":%ld,\"exp\":%ld,\"jku\":\"https://" BAAS "/1.0.0/certificates\","
            "\"jti\":\"%s\",\"di\":\"%s\",\"sn\":\"XAW10000000000\",\"bs:did\":\"%s\",\"hm\":true%s%s,\"nnex\":%s}",
            sub, (long)now, (long)now, (long)exp, jti, di, did, ver, app, nnex);
        static const char header[] = "{\"alg\":\"RS256\",\"kid\":\"nextendo-baas-key-1\",\"typ\":\"id_token\"}";
        size_t o = b64url(tok, (const uint8_t *)header, sizeof header - 1);
        tok[o++] = '.'; o += b64url(tok + o, (const uint8_t *)claims, (size_t)n);
        tok[o++] = '.'; b64url(tok + o, r + 64, 256);   // the signature nobody checks
    }
    size_t len = strlen(tok);
    if (out && cap) { size_t k = len < cap ? len : cap; memcpy(out, tok, k); if (k < cap) out[k] = 0; }
    pthread_mutex_unlock(&m);
    return len;
}

// CachedNintendoAccountInfo: birthday at 0, country at 0x20 (GetCountry; Mario Kart 8 Deluxe shows it as the player's flag), as Mush8 fills the 0x68 bytes the account service returns
uint32_t dmo_na_info(void *info) {
    if (!info) return RES_FAIL;
    uint8_t *p = info;
    memset(p, 0, 0x68);
    memcpy(p, "2000-01-01", 10);
    memcpy(p + 0x20, g_acct.country, 2);
    return RES_OK;
}
