// Web page applet (hle_web.c): ShowWebPageArg built as the SDK builds it (entries from +8, {u16 type, u16 size, u32 pad, data}); a Pokémon terms page comes back agreed with its callback
// URL (+ "/agree" for terms callbacks), read back the way the SDK 13 getters read WebPageReturnValue; any other page, or a callback outside the page's directory, stays cut
#include "../hle_web.c"
#include <mach-o/getsect.h>

static int checks, fails;
extern const struct mach_header_64 _mh_execute_header;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL: %s\n", msg); } } while (0)
static void *hook(const char *name) {
    unsigned long bytes = 0;
    const hle_force_ent *e = (const hle_force_ent *)getsectiondata(&_mh_execute_header, "__DATA", "swforce", &bytes);
    for (size_t i = 0; e && i < bytes / sizeof *e; i++) if (!strcmp(e[i].name, name)) return (void *)e[i].fn;
    return NULL;
}
static void arg(uint8_t *a, const char *url, const char *cb) {
    unsigned off = 8; memset(a, 0, 0x2000);
    uint8_t b[0xc00] = {0}; snprintf((char *)b, sizeof b, "%s", url); web_put(a, &off, 1, b, 0xc00);   // the SDK stores the URL fields at their full size
    if (cb) { memset(b, 0, 0x400); snprintf((char *)b, 0x400, "%s", cb); web_put(a, &off, 3, b, 0x400); }
}
int main(void) {
    Result (*show)(uint8_t *, const uint8_t *) = hook("_ZN2nn3web11ShowWebPageEPNS0_18WebPageReturnValueERKNS0_14ShowWebPageArgE");
    CHECK(show, "ShowWebPage hooked"); if (!show) return 1;
    static uint8_t a[0x2000], rv[0x2000]; unsigned n; const uint8_t *v;

    arg(a, "https://battle-sv.pokemon-home.com/scvi/battle-terms/index.html?lang=en", "https://battle-sv.pokemon-home.com/scvi/battle-terms/callback");
    CHECK(show(rv, a) == RES_OK, "Scarlet's Battle Stadium terms page: answered");
    v = web_tlv(rv, 1, &n); CHECK(v && n == 4 && *(const uint32_t *)v == 3, "exit reason LastUrl (3)");
    v = web_tlv(rv, 2, &n); CHECK(v && !strcmp((const char *)v, "https://battle-sv.pokemon-home.com/scvi/battle-terms/callback/agree"), "last URL: the callback + /agree");
    unsigned urlsize = v ? n : 0;
    v = web_tlv(rv, 3, &n); CHECK(v && n == 8 && *(const uint64_t *)v == urlsize && urlsize == strlen("https://battle-sv.pokemon-home.com/scvi/battle-terms/callback/agree") + 1, "last URL size counts the NUL");

    arg(a, "https://battle-za.pokemon-home.com/plza/ranked/start", "https://battle-za.pokemon-home.com/plza/ranked/callback");
    CHECK(show(rv, a) == RES_OK && (v = web_tlv(rv, 2, &n)) && !strcmp((const char *)v, "https://battle-za.pokemon-home.com/plza/ranked/callback"), "Legends Z-A ranked: callback as is (not a terms callback)");

    arg(a, "https://battle-sv.pokemon-home.com/scvi/battle-terms/index.html", "https://evil.example/scvi/battle-terms/callback");
    CHECK(show(rv, a) == RES_FAIL, "a callback on another site: cut");
    arg(a, "https://battle-sv.pokemon-home.com.evil.example/scvi/battle-terms/index.html", "https://battle-sv.pokemon-home.com.evil.example/scvi/battle-terms/callback");
    CHECK(show(rv, a) == RES_FAIL, "a host that only starts with pokemon-home.com: cut");
    arg(a, "https://battle-sv.pokemon-home.com/other/terms/index.html", "https://battle-sv.pokemon-home.com/other/terms/callback");
    CHECK(show(rv, a) == RES_FAIL, "a path outside /scvi/ and /plza/: cut");
    arg(a, "https://www.nintendo.com/news", NULL);
    CHECK(show(rv, a) == RES_FAIL, "any other page: cut");
    printf("t_web: %d checks, %d failed\n", checks, fails); return fails != 0;
}
