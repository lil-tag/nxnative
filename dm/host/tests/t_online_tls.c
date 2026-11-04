// Online play, stages 2 and 3 (hle_online_ssl.c, hle_online_account.c): the Nextendo sign-in from its shared file with the environment winning, the id token's claims,
// and a real TLS handshake, write and read through the nn::ssl takeovers against a local openssl s_server that stands in for Nextendo (DM_NEXTENDO_SERVER=127.0.0.1),
// then the refusal of TLS to an address that is not Nextendo's. Without openssl on PATH the TLS part is skipped
#include "../hle_online.c"
#include "../hle_online_ssl.c"
#include "../hle_online_account.c"
#include <signal.h>
#include <sys/wait.h>

module_t *g_mod; int g_nmod;
void dm_thread_native_wait(int entering) { (void)entering; }
void dm_guest_write_prepare(void *addr, size_t len) { (void)addr; (void)len; }
module_t *module_of(const void *p) { (void)p; return NULL; }   // the trace's caller names (DM_NEXTENDO_TRACE): no game modules here
int *dm_guest_errno(void) { return NULL; }
static char g_sysdata[300];   // the firmware's CertStore archive folder (hle_fs.c), a made-up one here
int dm_sysdata_dir(char *d, unsigned long n, uint64_t id) { if (id != 0x0100000000000800ull || !g_sysdata[0]) return 0; snprintf(d, n, "%s", g_sysdata); return 1; }
static struct { const void *k; void *v; } g_map[64];
void *objmap_get(const void *k) { for (int i = 0; i < 64; i++) if (g_map[i].k == k) return g_map[i].v; return NULL; }
void objmap_put(const void *k, void *v) { for (int i = 0; i < 64; i++) if (!g_map[i].k) { g_map[i].k = k; g_map[i].v = v; return; } }
void *objmap_del(const void *k) { for (int i = 0; i < 64; i++) if (g_map[i].k == k) { void *v = g_map[i].v; g_map[i].k = NULL; return v; } return NULL; }

static int checks, fails;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL: %s\n", msg); } } while (0)

static size_t unb64url(uint8_t *out, const char *in, size_t n) {
    size_t o = 0; uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < n; i++) {
        const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_", *p = strchr(T, in[i]);
        if (!p || !in[i]) break;
        acc = acc << 6 | (uint32_t)(p - T); bits += 6;
        if (bits >= 8) { bits -= 8; out[o++] = (uint8_t)(acc >> bits); }
    }
    out[o] = 0;
    return o;
}

static int free_port(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in a = {.sin_len = sizeof a, .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)}; socklen_t l = sizeof a;
    bind(s, (struct sockaddr *)&a, sizeof a); getsockname(s, (struct sockaddr *)&a, &l); close(s);
    return ntohs(a.sin_port);
}
static int guest_connect(int port) {   // a guest TCP socket connected to loopback, as a game would make it
    for (int tries = 0; tries < 50; tries++) {
        int g = dmo_socket(AF_INET, SOCK_STREAM, 0);
        uint8_t sa[16] = {0, AF_INET, (uint8_t)(port >> 8), (uint8_t)port, 127, 0, 0, 1};
        if (dmo_connect(g, sa, sizeof sa) == 0) return g;
        dmo_close(g); usleep(100000);
    }
    return -1;
}

int main(void) {
    setenv("DM_NEXTENDO", "1", 1);
    char dir[] = "/tmp/t_online_tls.XXXXXX";
    if (!mkdtemp(dir)) return 1;

    // ---------- the sign-in: the shared file, the environment winning ----------
    char path[256]; snprintf(path, sizeof path, "%s/nextendo.env", dir);
    FILE *f = fopen(path, "w");
    fputs("# written by a front end\nDM_NEXTENDO_PID=4242\nDM_NEXTENDO_TOKEN=4242.tester.9999999999.sig\nDM_NEXTENDO_NAME=fromfile\nDM_NEXTENDO_COUNTRY=ca\n", f); fclose(f);
    setenv("DM_NEXTENDO_ACCOUNT", path, 1);
    setenv("DM_NEXTENDO_NAME", "fromenv", 1);
    CHECK(dmo_signed_in() && dmo_pid() == 4242, "signed in from the shared file");
    CHECK(!strcmp(dmo_name(), "fromenv"), "the environment wins over the file");
    char tok[4096]; size_t len = dmo_id_token(tok, sizeof tok);
    char *d1 = strchr(tok, '.'), *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
    CHECK(len == strlen(tok) && d1 && d2 && !strchr(d2 + 1, '.'), "the id token has three parts");
    uint8_t claims[3000] = {0};
    if (d1 && d2) unb64url(claims, d1 + 1, (size_t)(d2 - d1 - 1));
    CHECK(strstr((char *)claims, "\"nnex\":\"4242.tester.9999999999.sig\"") != NULL, "the account token rides in nnex");
    CHECK(strstr((char *)claims, "\"aud\":\"ed9e2f05d286f7b8\"") && strstr((char *)claims, "\"hm\":true") && strstr((char *)claims, "\"iss\":\"https://e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com\""), "the claims Nextendo reads");
    CHECK(!strstr((char *)claims, "\"tv\"") && !strstr((char *)claims, "app_id"), "no version for an unregistered build, no app_id for this game");
    char small[16]; CHECK(dmo_id_token(small, sizeof small) == len && !memcmp(small, tok, sizeof small), "a short buffer gets the start, and the full length");
    CHECK(dmo_id_token(NULL, 0) == len, "the token is cached");
    uint8_t info[0x68]; dmo_na_info(info);
    CHECK(!memcmp(info, "2000-01-01", 10) && info[0x20] == 'C' && info[0x21] == 'A', "the cached account info carries the country in capitals");

    // ---------- the firmware's root certificates, as Super Mario Party Jamboree reads them ----------
    {
        snprintf(g_sysdata, sizeof g_sysdata, "%s", dir);
        char bp[300]; snprintf(bp, sizeof bp, "%s/ssl_TrustedCerts.bdf", dir);
        uint32_t store[10] = {0x546c7373, 2, 1, 1, 3, 32, 7, 0, 5, 35};   // "sslT", 2 entries {id, status, size, offset}
        FILE *bf = fopen(bp, "wb"); fwrite(store, 4, 10, bf); fwrite("abcdefgh", 1, 8, bf); fclose(bf);
        int32_t every = -1, seven = 7, three = 3; uint32_t need = 0;
        CHECK(dmo_ssl_builtin_size(&need, &every, 1) == RES_OK && need == 3 * 0x18 + 4 + 8, "the size for all: two entries, the end mark, the data 4-aligned");
        static _Alignas(8) uint8_t cb[128]; void *out = NULL;
        CHECK(dmo_ssl_builtin(&out, cb, sizeof cb, &every, 1) == RES_OK && out == cb, "all of them");
        uint8_t **p0 = (uint8_t **)(cb + 0x10), **p1 = (uint8_t **)(cb + 0x28);
        CHECK(*(int32_t *)cb == 1 && *(uint32_t *)(cb + 4) == 1 && *(uint64_t *)(cb + 8) == 3 && !memcmp(*p0, "abc", 3), "first: id, status, size and a pointer to its data");
        CHECK(*(int32_t *)(cb + 0x18) == 7 && *(uint64_t *)(cb + 0x20) == 5 && !memcmp(*p1, "defgh", 5) && *(int32_t *)(cb + 0x30) == -1, "second, then the end mark");
        CHECK(dmo_ssl_builtin_size(&need, &seven, 1) == RES_OK && need == 0x18 + 8 && dmo_ssl_builtin(&out, cb, sizeof cb, &seven, 1) == RES_OK && *(int32_t *)cb == 7, "one asked for by its ID, no end mark");
        CHECK(dmo_ssl_builtin(&out, cb, sizeof cb, &three, 1) != RES_OK && dmo_ssl_builtin(&out, cb, 40, &every, 1) != RES_OK, "a missing ID or a short buffer fails");
    }

    // ---------- TLS against a stand-in Nextendo ----------
    if (system("command -v openssl >/dev/null 2>&1")) { printf("t_online_tls: %d checks, %d failed (TLS skipped: no openssl)\n", checks, fails); return fails != 0; }
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "cd %s && openssl req -x509 -newkey rsa:2048 -nodes -keyout k.pem -out c.pem -days 1 -subj /CN=stand-in >/dev/null 2>&1", dir);
    CHECK(system(cmd) == 0, "a self-signed certificate, as Nextendo's");
    int port = free_port();
    pid_t srv = fork();
    if (srv == 0) {
        char p[16]; snprintf(p, sizeof p, "%d", port);
        if (chdir(dir)) _exit(1);
        freopen("/dev/null", "w", stdout); freopen("/dev/null", "w", stderr);
        execlp("openssl", "openssl", "s_server", "-accept", p, "-cert", "c.pem", "-key", "k.pem", "-www", "-tls1_2", "-alpn", "h2,http/1.1", (char *)NULL);
        _exit(1);
    }
    setenv("DM_NEXTENDO_SERVER", "127.0.0.1", 1);
    int g = guest_connect(port);
    CHECK(g >= 3, "connected to the stand-in");
    static _Alignas(16) uint8_t ctx[0x40], cn[0x40];
    CHECK(h_ssl_CtxCreate(ctx, 1) == RES_OK && h_ssl_CnCreate(cn, ctx) == RES_OK, "context and connection");
    uint64_t ctxid = 0; int ov = 7;   // Smash's libcurl reads these before the handshake; when they failed it closed every connection
    CHECK(h_ssl_CtxId(ctx, &ctxid) == RES_OK && ctxid == 1 && h_ssl_CtxOpt(ctx, 1, 1) == RES_OK && h_ssl_CtxGetOpt(ctx, &ov, 1) == RES_OK && !ov && h_ssl_CtxPolicy(ctx, "1.2", 3) == RES_OK, "context ID and options online");
    char cipher[72]; uint32_t nerr = 9, total = 9;
    CHECK(h_ssl_CnCipher(cn, cipher) == RES_OK && !strcmp(cipher + 64, "TLSv1.2") && h_ssl_CnVerifyErrs(cn, NULL, &nerr, &total, 0) == RES_OK && !nerr && !total && h_ssl_CnPending0(cn) == 0, "the rest of the connection's surface");
    uint64_t pki = 0; CHECK(dmo_ssl_import(&pki) == RES_OK && pki, "a certificate import is accepted");
    CHECK(h_ssl_CnSock(cn, g) == RES_OK && h_ssl_CnSock(cn, g) == R_IN_USE, "the socket is taken once");
    CHECK(h_ssl_CnHost(cn, "g23380901-lp1.s.n.srv.nintendo.net", 34) == RES_OK, "host name");
    static const uint8_t offer[] = "\x02h2\x08http/1.1";   // gRPC's offer (Super Mario Party Jamboree)
    CHECK(h_ssl_CnSetAlpn(cn, offer, sizeof offer - 1) == RES_OK, "the game offers h2 and http/1.1");
    uint32_t sz = 9, cnt = 9;
    CHECK(h_ssl_CnHandshake2(cn, &sz, &cnt) == RES_OK && sz == 0 && cnt == 0, "the handshake completes with Nextendo's own certificate");
    int st = 9; uint8_t proto[16] = {0}; uint32_t pn = 0;
    CHECK(h_ssl_CnGetAlpn(cn, &st, proto, &pn, sizeof proto) == RES_OK && st == 1 && pn == 2 && !memcmp(proto, "h2", 2), "the server agreed to h2, as gRPC reads it");
    const char req[] = "GET / HTTP/1.0\r\n\r\n"; int w = 0;
    CHECK(h_ssl_CnWrite3(cn, req, &w, sizeof req - 1) == RES_OK && w == (int)sizeof req - 1, "write through TLS");
    char peek[8] = {0}; int pk = 0;
    CHECK(h_ssl_CnPeek(cn, peek, &pk, 4) == RES_OK && pk == 4 && !memcmp(peek, "HTTP", 4), "peek sees the reply without taking it");
    int pend = 0; CHECK(h_ssl_CnPending(cn, &pend) == RES_OK && pend >= 4, "pending counts what was read ahead");
    char buf[256] = {0}; int r = 0;
    CHECK(h_ssl_CnRead3(cn, buf, &r, sizeof buf - 1) == RES_OK && r >= 12 && !strncmp(buf, "HTTP/1.0 200", 12), "read the reply through TLS, from its first byte");
    CHECK(h_ssl_CnDestroy(cn) == RES_OK && !objmap_get(cn), "destroy frees the connection");
    dmo_close(g);

    // gRPC's dual-stack socket: IPv6, connected to the IPv4 address as ::ffff:a.b.c.d (the guest's sockaddr_in6 is BSD's, family 28)
    {
        int g6 = dmo_socket(28, SOCK_STREAM, 0);
        uint8_t sa6[28] = {28, 28, (uint8_t)(port >> 8), (uint8_t)port}; sa6[18] = sa6[19] = 0xff; sa6[20] = 127; sa6[23] = 1;
        CHECK(g6 >= 3 && dmo_connect(g6, sa6, sizeof sa6) == 0, "an IPv6 socket connects to the IPv4-mapped address");
        CHECK(h_ssl_CnCreate(cn, ctx) == RES_OK && h_ssl_CnSock(cn, g6) == RES_OK && h_ssl_CnHandshake(cn) == RES_OK, "TLS over it: the peer is read through the mapping");
        h_ssl_CnDestroy(cn); dmo_close(g6);
    }

    // the same server, not one of Nextendo's two addresses: an address the game was handed (Mario Tennis Aces' game server, from Nextendo's login) is trusted,
    // and refused before any TLS once an internet name resolved to it
    unsetenv("DM_NEXTENDO_SERVER");
    g = guest_connect(port);
    CHECK(h_ssl_CnCreate(cn, ctx) == RES_OK && h_ssl_CnSock(cn, g) == RES_OK && h_ssl_CnHandshake(cn) == RES_OK, "TLS to an address no name lookup gave: trusted");
    h_ssl_CnDestroy(cn); dmo_close(g);
    void *lh = NULL; CHECK(dmo_getaddrinfo("localhost", NULL, NULL, &lh) == 0, "an internet name resolves as it is"); dmo_freeaddrinfo(lh);
    g = guest_connect(port);
    CHECK(h_ssl_CnCreate(cn, ctx) == RES_OK && h_ssl_CnSock(cn, g) == RES_OK && h_ssl_CnHandshake(cn) == R_ABORT, "TLS to an internet name's address is refused");
    st = 9; CHECK(h_ssl_CnGetAlpn(cn, &st, proto, &pn, sizeof proto) == RES_OK && st == 0 && pn == 0, "no protocol without a handshake");
    h_ssl_CnDestroy(cn); dmo_close(g);

    kill(srv, SIGTERM); waitpid(srv, NULL, 0);
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir); system(cmd);
    printf("t_online_tls: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
