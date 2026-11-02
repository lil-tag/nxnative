// nn::ssl (TLS): offline every call that would reach the ssl service fails, as before; with online play (DM_NEXTENDO=1, hle_online.c) a connection runs TLS over the game's own
// socket through Secure Transport, the Apple TLS API that works over a socket the caller already connected, as nn::ssl does. TLS 1.2, the Switch's own ceiling.
// Only Nextendo's addresses are accepted, and their certificates are Nextendo's own: accepted without a chain check, as Mush8 (ssl.rs) and Ryujinx-Nextendo do.
// The C interface (nnsslConnectionRead, …) is the SDK's own code calling these C++ methods with the same arguments (Smash's sdk), so only the methods are taken over.
// Results are the ssl service's (module 123), as Mush8 answers them: Mario Kart 8 Deluxe plays online on them
#include "hle.h"
#include "online.h"
#include <Security/SecureTransport.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#pragma clang diagnostic ignored "-Wdeprecated-declarations"   // Secure Transport is deprecated, and still the only Apple TLS that takes an existing socket

#define SSL_RC(d) (123u | (d) << 9)
enum { R_NO_SOCKET = SSL_RC(103), R_INVALID_SOCKET = SSL_RC(106), R_IN_USE = SSL_RC(203), R_WOULD_BLOCK = SSL_RC(204), R_TIMEOUT = SSL_RC(205), R_RESET = SSL_RC(209), R_ABORT = SSL_RC(210) };
enum { IO_BLOCKING = 1, IO_NONBLOCKING = 2, HANDSHAKE_MS = 30000, POLL_READ = 1, POLL_WRITE = 2 };

typedef struct {
    int fd, gfd, nonblock, verify, cache;   // fd: the Mac's, a duplicate of the game's socket (the game may close its own)
    uint32_t opts, last;
    uint64_t id;
    char host[256];
    SSLContextRef tls;
    int eof;
    size_t npeek; uint8_t peek[16384];   // read ahead for Peek
    unsigned nreads;   // records read, for the dump
    uint8_t alpn[64]; unsigned nalpn;   // the protocols the game offers (SetNextAlpnProto), wire format: a length byte before each name
} conn_t;
static _Atomic uint64_t g_ids;
static conn_t *conn(void *self) { return self ? objmap_get(self) : NULL; }

// ---------- the socket under TLS: reads never wait (the caller does, by its io mode), writes wait until sent ----------
static int wait_fd(int fd, short ev, int ms) {
    struct pollfd p = {fd, ev, 0}; int r;
    do r = poll(&p, 1, ms); while (r < 0 && errno == EINTR);
    return r > 0;
}
static OSStatus io_read(SSLConnectionRef ref, void *data, size_t *len) {
    int fd = (int)(intptr_t)ref; size_t want = *len, got = 0;
    while (got < want) {
        ssize_t n = recv(fd, (char *)data + got, want - got, MSG_DONTWAIT);
        if (n > 0) { got += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        *len = got;
        return n == 0 ? errSSLClosedGraceful : errno == EAGAIN ? errSSLWouldBlock : errSSLClosedAbort;
    }
    return noErr;
}
static OSStatus io_write(SSLConnectionRef ref, const void *data, size_t *len) {
    int fd = (int)(intptr_t)ref; size_t want = *len, put = 0;
    while (put < want) {
        ssize_t n = send(fd, (const char *)data + put, want - put, MSG_DONTWAIT);
        if (n > 0) { put += (size_t)n; continue; }
        if (n < 0 && (errno == EINTR || (errno == EAGAIN && wait_fd(fd, POLLOUT, HANDSHAKE_MS)))) continue;
        *len = put;
        return errSSLClosedAbort;
    }
    return noErr;
}

// ---------- context: nothing to keep; certificates the game imports are not needed for Nextendo's ----------
Result dmo_ssl_import(uint64_t *id) { if (id) *id = ++g_ids; return RES_OK; }

// ---------- the firmware's root certificates (BuiltInManager) ----------
// Super Mario Party Jamboree's online library builds its trust store from them (main+0xc74c3c) and parses each one; with none it never connects (error 2321-4992 after the
// name lookup). They come from the player's firmware as Ryujinx reads them: ssl_TrustedCerts.bdf of the CertStore archive 0100000000000800 (nx_extract --sysdata, as MiiModel),
// "sslT", a count, entries {id, status, size, offset from the end of the header} and the DER data. The answer is the ssl service's as the SDK hands it on (sdk+0x3cd8c0):
// 0x18-byte entries {id, status, u64 size, data pointer}, ended by id -1 when all were asked for (an ID -1), then the data, each 4-aligned
#define SYSDATA_CERTSTORE 0x0100000000000800ull
static uint8_t *g_bdf; static size_t g_nbdf;
static pthread_once_t g_bdf_once = PTHREAD_ONCE_INIT;
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void bdf_read(void) {
    char dir[1024], path[1100]; FILE *f = NULL;
    if (dm_sysdata_dir(dir, sizeof dir, SYSDATA_CERTSTORE)) { snprintf(path, sizeof path, "%s/ssl_TrustedCerts.bdf", dir); f = fopen(path, "rb"); }
    if (!f) { LOG("[online] no root certificates: extract the firmware's CertStore (nx_extract --sysdata <Ryujinx>/bis/system/Contents/registered 0100000000000800 <sysdata>/0100000000000800)"); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t *b = n > 8 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) == (size_t)n && !memcmp(b, "sslT", 4) && 8 + (uint64_t)rd32(b + 4) * 16 <= (uint64_t)n) {
        int ok = 1;
        for (uint32_t i = 0; i < rd32(b + 4); i++) ok &= 8 + (uint64_t)rd32(b + 8 + 16 * i + 12) + rd32(b + 8 + 16 * i + 8) <= (uint64_t)n;
        if (ok) { g_bdf = b; g_nbdf = (size_t)n; b = NULL; }
    }
    if (b || !g_bdf) LOG("[online] root certificates: %s is not a certificate store", path);
    free(b); fclose(f);
}
// buf NULL: only the size the answer needs
static Result builtin(uint8_t *buf, size_t cap, const int32_t *ids, unsigned n, uint32_t *need, uint32_t *written) {
    pthread_once(&g_bdf_once, bdf_read);
    if (!g_bdf) return RES_FAIL;
    uint32_t count = rd32(g_bdf + 4); int all = 0;
    for (unsigned i = 0; ids && i < n; i++) all |= ids[i] == -1;
    unsigned ne = all ? count : n;
    for (int pass = 0; pass < 2; pass++) {
        size_t at = (size_t)(ne + all) * 0x18;
        for (unsigned i = 0; i < ne; i++) {
            const uint8_t *e = all ? g_bdf + 8 + 16 * i : NULL;
            for (uint32_t j = 0; !e && j < count; j++) if ((int32_t)rd32(g_bdf + 8 + 16 * j) == ids[i]) e = g_bdf + 8 + 16 * j;
            if (!e) return RES_FAIL;   // a certificate this firmware does not have
            uint32_t sz = rd32(e + 8);
            if (pass) {
                uint8_t *o = buf + 0x18 * i, *d = buf + at; uint64_t s64 = sz;
                memcpy(o, e, 8); memcpy(o + 8, &s64, 8); memcpy(d, g_bdf + 8 + rd32(e + 12), sz); memcpy(o + 0x10, &d, 8);
            }
            at += (sz + 3) & ~3u;
        }
        if (!pass) { if (need) *need = (uint32_t)at; if (!buf) return RES_OK; if (cap < at) return RES_FAIL; continue; }
        if (all) { memset(buf + 0x18 * ne, 0, 0x18); memset(buf + 0x18 * ne, 0xff, 4); }
        if (written) *written = ne + all;
    }
    return RES_OK;
}
Result dmo_ssl_builtin_size(uint32_t *out, const int32_t *ids, unsigned n) { uint32_t need = 0; Result r = builtin(NULL, 0, ids, n, &need, NULL); if (out) *out = r ? 0 : need; return r; }
Result dmo_ssl_builtin(void **out, uint8_t *buf, unsigned cap, const int32_t *ids, unsigned n) {
    Result r = builtin(buf, cap, ids, n, NULL, NULL);
    if (out) *out = r ? NULL : buf;
    return r;
}

// ---------- connection ----------
static Result c_create(void *self) {
    conn_t *c = calloc(1, sizeof *c);
    if (!c) return R_ABORT;
    c->fd = c->gfd = -1; c->id = ++g_ids; c->verify = 3;
    objmap_put(self, c);
    if (dmo_trace()) LOG("[online] trace TLS connection %p created", self);
    return RES_OK;
}
static Result c_destroy(void *self) {
    conn_t *c = objmap_del(self);
    if (!c) return RES_OK;
    if (c->tls) { SSLClose(c->tls); CFRelease(c->tls); }
    if (c->fd >= 0) close(c->fd);
    free(c);
    return RES_OK;
}
static Result c_set_socket(conn_t *c, int gfd) {
    if (c->fd >= 0) return R_IN_USE;
    int h = dmo_host_fd(gfd);
    if (h < 0 || (c->fd = dup(h)) < 0) return R_INVALID_SOCKET;
    c->gfd = gfd;
    if (dmo_trace()) LOG("[online] trace TLS connection takes socket %d", gfd);
    return RES_OK;
}
static Result c_handshake(conn_t *c) {
    if (dmo_trace()) LOG("[online] trace TLS handshake on socket %d (host %s)", c->gfd, c->host);
    if (c->fd < 0) return R_NO_SOCKET;
    // the peer's IPv4 address, also behind an IPv6 socket (gRPC, Super Mario Party Jamboree, connects dual-stack sockets to ::ffff:a.b.c.d once its IPv6 check passes)
    struct sockaddr_storage peer = {0}; socklen_t len = sizeof peer; char ip[INET_ADDRSTRLEN] = "?"; struct in_addr p4 = {0}; int v4 = 0;
    if (getpeername(c->fd, (struct sockaddr *)&peer, &len) == 0) {
        const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)&peer;
        if (peer.ss_family == AF_INET) { p4 = ((const struct sockaddr_in *)&peer)->sin_addr; v4 = 1; }
        else if (peer.ss_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) { memcpy(&p4, s6->sin6_addr.s6_addr + 12, 4); v4 = 1; }
        if (v4) inet_ntop(AF_INET, &p4, ip, sizeof ip);
    }
    if (!v4 || !dmo_is_trusted(p4.s_addr)) { LOG("[online] TLS to %s (%s) refused: not a Nextendo server", c->host, ip); return R_ABORT; }
    char name[256]; struct in_addr numeric;
    if (c->host[0] && inet_pton(AF_INET, c->host, &numeric) != 1) snprintf(name, sizeof name, "%s", c->host);
    else if (!dmo_host_for(p4.s_addr, name, sizeof name)) name[0] = 0;
    c->tls = SSLCreateContext(NULL, kSSLClientSide, kSSLStreamType);
    if (!c->tls) return R_ABORT;
    SSLSetIOFuncs(c->tls, io_read, io_write);
    SSLSetConnection(c->tls, (SSLConnectionRef)(intptr_t)c->fd);
    if (name[0]) SSLSetPeerDomainName(c->tls, name, strlen(name));
    SSLSetSessionOption(c->tls, kSSLSessionOptionBreakOnServerAuth, true);
    SSLSetProtocolVersionMin(c->tls, kTLSProtocol1);
    SSLSetProtocolVersionMax(c->tls, kTLSProtocol12);
    if (c->nalpn) {   // offered only when the game asks, as Ryujinx-Nextendo does for npln: gRPC needs h2, while NEX's WebSocket upgrade needs http/1.1
        CFMutableArrayRef protos = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
        for (unsigned i = 0; i < c->nalpn && i + 1 + c->alpn[i] <= c->nalpn; i += 1 + c->alpn[i]) {
            CFStringRef s = CFStringCreateWithBytes(NULL, c->alpn + i + 1, c->alpn[i], kCFStringEncodingASCII, false);
            if (s) { CFArrayAppendValue(protos, s); CFRelease(s); }
        }
        SSLSetALPNProtocols(c->tls, protos); CFRelease(protos);
    }
    OSStatus st; int timed_out = 0;
    dm_thread_native_wait(1);
    for (;;) {
        st = SSLHandshake(c->tls);
        if (st == errSSLServerAuthCompleted) continue;   // a Nextendo address: its own certificate is accepted
        if (st != errSSLWouldBlock) break;
        if (!wait_fd(c->fd, POLLIN, HANDSHAKE_MS)) { timed_out = 1; break; }
    }
    dm_thread_native_wait(0);
    if (timed_out) { LOG("[online] TLS to %s (%s): handshake timed out", name, ip); return R_TIMEOUT; }
    if (st) { LOG("[online] TLS to %s (%s) failed: OSStatus %d", name, ip, (int)st); return R_ABORT; }
    LOG("[online] TLS connected to %s (%s)", name[0] ? name : "?", ip);
    return RES_OK;
}
// Read (consume) or Peek: plaintext already read ahead first; then the connection, waiting in blocking mode
// DM_NEXTENDO_DUMP=<file>: every TLS record the game reads, and every one it writes once the connection has read 4 (past the sign-in, which carries the
// token), appended as [direction 'R'/'W', u8][game socket, i32][length, u32][bytes]: the game server's messages in the clear, to see what a game does with them
static void tls_dump(conn_t *c, char dir, const void *b, size_t n) {
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER; const char *path = getenv("DM_NEXTENDO_DUMP");
    if (!path || !*path || !n || (dir == 'W' && c->nreads < 4)) return;
    pthread_mutex_lock(&m);
    FILE *f = fopen(path, "ab");
    if (f) { int32_t s = c->gfd; uint32_t l = (uint32_t)n; fputc(dir, f); fwrite(&s, 4, 1, f); fwrite(&l, 4, 1, f); fwrite(b, 1, n, f); fclose(f); }
    pthread_mutex_unlock(&m);
}
static Result c_read(conn_t *c, char *b, uint32_t n, int *out, int consume) {
    if (!c->tls) return R_NO_SOCKET;
    for (;;) {
        if (c->npeek || !n) {
            size_t k = c->npeek < n ? c->npeek : n;
            memcpy(b, c->peek, k);
            if (consume) { tls_dump(c, 'R', b, k); c->nreads++; memmove(c->peek, c->peek + k, c->npeek - k); c->npeek -= k; }
            if (out) *out = (int)k;
            return RES_OK;
        }
        if (c->eof) { if (out) *out = 0; return RES_OK; }
        size_t got = 0;
        OSStatus st = consume ? SSLRead(c->tls, b, n, &got) : SSLRead(c->tls, c->peek, sizeof c->peek, &got);
        if (dmo_trace()) LOG("[online] trace TLS read socket %d: asked %u, got %zu, status %d%s", c->gfd, n, got, (int)st, c->nonblock ? " (non-blocking)" : "");
        if (got) {
            if (!consume) { c->npeek = got; continue; }
            tls_dump(c, 'R', b, got); c->nreads++;
            if (out) *out = (int)got;
            return RES_OK;
        }
        if (st == errSSLClosedGraceful || st == errSSLClosedNoNotify) { c->eof = 1; continue; }
        if (st != errSSLWouldBlock) { LOG("[online] TLS read failed: OSStatus %d", (int)st); return R_RESET; }
        if (c->nonblock) return R_WOULD_BLOCK;
        dm_thread_native_wait(1); wait_fd(c->fd, POLLIN, -1); dm_thread_native_wait(0);
    }
}
static Result c_write(conn_t *c, const char *b, uint32_t n, int *out) {
    if (!c->tls) return R_NO_SOCKET;
    size_t put = 0;
    dm_thread_native_wait(1);
    OSStatus st = SSLWrite(c->tls, b, n, &put);
    dm_thread_native_wait(0);
    if (st && !put) { LOG("[online] TLS write failed: OSStatus %d", (int)st); return R_RESET; }
    if (dmo_trace()) LOG("[online] trace TLS write socket %d: %u bytes, %zu sent, status %d", c->gfd, n, put, (int)st);   // sizes only: requests carry the sign-in token
    tls_dump(c, 'W', b, put);
    if (out) *out = (int)put;
    return RES_OK;
}
static int c_pending(conn_t *c) {
    size_t n = 0;
    if (c->tls) SSLGetBufferedReadSize(c->tls, &n);
    return (int)(c->npeek + n);
}
static Result c_poll(conn_t *c, uint32_t *out, const uint32_t *in, uint32_t ms) {
    uint32_t want = in ? *in : 0, got = 0;
    if (want & POLL_READ && c_pending(c)) got |= POLL_READ;
    if (!got && c->fd >= 0) {
        struct pollfd p = {c->fd, (short)((want & POLL_READ ? POLLIN : 0) | (want & POLL_WRITE ? POLLOUT : 0)), 0};
        dm_thread_native_wait(1); int r = poll(&p, 1, (int)ms); dm_thread_native_wait(0);
        if (r > 0) got = (p.revents & POLLIN ? POLL_READ : 0) | (p.revents & POLLOUT ? POLL_WRITE : 0);
    }
    if (out) *out = got;
    return RES_OK;
}

// ---------- the takeovers: offline the cut answer, online the connection above ----------
#define ONLINE_CONN(self) conn_t *c = dm_online() ? conn(self) : NULL
#define SSF(m, cname, ...) HLE_FORCE(m, "ssl service: offline fails (cut); online, TLS to Nextendo (hle_online_ssl.c)", Result, cname, __VA_ARGS__)
HLE_FORCE("_ZN2nn3ssl10InitializeEv", "same as nnsslInitialize: succeeds, creates no session (cut)", Result, h_ssl_InitCpp, void) { return RES_OK; }
HLE_FORCE("_ZN2nn3ssl8FinalizeEv", "pairs with Initialize (cut)", Result, h_ssl_FinCpp, void) { return RES_OK; }
SSF("_ZN2nn3ssl7Context6CreateENS1_10SslVersionE", h_ssl_CtxCreate, void *self, int v) { (void)self; (void)v; return dm_online() ? RES_OK : RES_FAIL; }
SSF("_ZN2nn3ssl7Context7DestroyEv", h_ssl_CtxDestroy, void *self) { (void)self; return dm_online() ? RES_OK : RES_FAIL; }
SSF("_ZN2nn3ssl7Context19RegisterInternalPkiEPmNS1_11InternalPkiE", h_ssl_CtxRegPki, void *self, uint64_t *id, int p) { (void)self; (void)p; return dm_online() ? dmo_ssl_import(id) : RES_FAIL; }
SSF("_ZN2nn3ssl7Context9RemovePkiEm", h_ssl_CtxRemovePki, void *self, uint64_t id) { (void)self; (void)id; return dm_online() ? RES_OK : RES_FAIL; }
// Smash's libcurl reads the context ID and sets context options before it hands a socket to a connection; left to the SDK they asked the absent
// ssl service, failed, and curl closed every connection to Nextendo right after connect. The ID is the one Connection::GetContextId reports
SSF("_ZN2nn3ssl7Context12GetContextIdEPm", h_ssl_CtxId, void *self, uint64_t *id) { (void)self; if (!dm_online()) return RES_FAIL; if (id) *id = 1; return RES_OK; }
SSF("_ZN2nn3ssl7Context9SetOptionENS1_13ContextOptionEi", h_ssl_CtxOpt, void *self, int o, int v) { (void)self; (void)o; (void)v; return dm_online() ? RES_OK : RES_FAIL; }
SSF("_ZN2nn3ssl7Context9GetOptionEPiNS1_13ContextOptionE", h_ssl_CtxGetOpt, void *self, int *v, int o) { (void)self; (void)o; if (!dm_online()) return RES_FAIL; if (v) *v = 0; return RES_OK; }
SSF("_ZN2nn3ssl7Context12AddPolicyOidEPKcj", h_ssl_CtxPolicy, void *self, const char *oid, unsigned n) { (void)self; (void)oid; (void)n; return dm_online() ? RES_OK : RES_FAIL; }
SSF("_ZN2nn3ssl14BuiltInManager28GetBuiltInCertificateBufSizeEPjPNS0_15CaCertificateIdEj", h_ssl_BiBufSize, uint32_t *o, void *ids, unsigned n) { if (dm_online()) return dmo_ssl_builtin_size(o, ids, n); (void)ids; (void)n; if (o) *o = 0; return RES_FAIL; }
SSF("_ZN2nn3ssl14BuiltInManager22GetBuiltInCertificatesEPPNS1_22BuiltInCertificateInfoEPhjPNS0_15CaCertificateIdEj", h_ssl_BiCerts, void **o, uint8_t *b, unsigned bs, void *ids, unsigned n) { if (dm_online()) return dmo_ssl_builtin(o, b, bs, ids, n); (void)o; (void)b; (void)bs; (void)ids; (void)n; return RES_FAIL; }
SSF("_ZN2nn3ssl10Connection6CreateEPNS0_7ContextE", h_ssl_CnCreate, void *self, void *ctx) { (void)ctx; return dm_online() ? c_create(self) : RES_FAIL; }
SSF("_ZN2nn3ssl10Connection7DestroyEv", h_ssl_CnDestroy, void *self) { return dm_online() ? c_destroy(self) : RES_FAIL; }
SSF("_ZN2nn3ssl10Connection11DoHandshakeEv", h_ssl_CnHandshake, void *self) { ONLINE_CONN(self); return c ? c_handshake(c) : RES_FAIL; }
SSF("_ZN2nn3ssl10Connection11DoHandshakeEPjS2_", h_ssl_CnHandshake2, void *self, uint32_t *size, uint32_t *count) {
    ONLINE_CONN(self); if (!c) return RES_FAIL;
    if (size) *size = 0;
    if (count) *count = 0;   // no server certificate is handed back
    return c_handshake(c);
}
SSF("_ZN2nn3ssl10Connection11DoHandshakeEPjS2_Pcj", h_ssl_CnHandshake4, void *self, uint32_t *size, uint32_t *count, char *buf, unsigned n) {
    (void)buf; (void)n; ONLINE_CONN(self); if (!c) return RES_FAIL;
    if (size) *size = 0;
    if (count) *count = 0;
    return c_handshake(c);
}
SSF("_ZN2nn3ssl10Connection11SetHostNameEPKcj", h_ssl_CnHost, void *self, const char *h, unsigned n) {
    ONLINE_CONN(self); if (!c) return RES_FAIL;
    size_t k = n < sizeof c->host - 1 ? n : sizeof c->host - 1;
    memcpy(c->host, h, k); c->host[k] = 0;
    return RES_OK;
}
SSF("_ZN2nn3ssl10Connection11GetHostNameEPcPjj", h_ssl_CnGetHost, void *self, char *out, uint32_t *len, unsigned cap) {
    ONLINE_CONN(self); if (!c) return RES_FAIL;
    uint32_t k = (uint32_t)strlen(c->host);
    if (out && cap) { uint32_t w = k < cap ? k : cap; memcpy(out, c->host, w); }
    if (len) *len = k;
    return RES_OK;
}
SSF("_ZN2nn3ssl10Connection9SetIoModeENS1_6IoModeE", h_ssl_CnIoMode, void *self, int m) { ONLINE_CONN(self); if (!c) return RES_FAIL; c->nonblock = m != IO_BLOCKING; return RES_OK; }
SSF("_ZN2nn3ssl10Connection9GetIoModeEPNS1_6IoModeE", h_ssl_CnGetIoMode, void *self, int *m) { ONLINE_CONN(self); if (!c) return RES_FAIL; if (m) *m = c->nonblock ? IO_NONBLOCKING : IO_BLOCKING; return RES_OK; }
SSF("_ZN2nn3ssl10Connection19SetSessionCacheModeENS1_16SessionCacheModeE", h_ssl_CnCache, void *self, int m) { ONLINE_CONN(self); if (!c) return RES_FAIL; c->cache = m; return RES_OK; }
SSF("_ZN2nn3ssl10Connection19SetSocketDescriptorEi", h_ssl_CnSock, void *self, int s) { ONLINE_CONN(self); return c ? c_set_socket(c, s) : RES_FAIL; }
SSF("_ZN2nn3ssl10Connection19GetSocketDescriptorEPi", h_ssl_CnGetSock, void *self, int *s) { ONLINE_CONN(self); if (!c) return RES_FAIL; if (s) *s = c->gfd; return RES_OK; }
SSF("_ZN2nn3ssl10Connection15SetVerifyOptionENS1_12VerifyOptionE", h_ssl_CnVerify, void *self, int v) { ONLINE_CONN(self); if (!c) return RES_FAIL; c->verify = v; return RES_OK; }
SSF("_ZN2nn3ssl10Connection15GetVerifyOptionEPNS1_12VerifyOptionE", h_ssl_CnGetVerify, void *self, int *v) { ONLINE_CONN(self); if (!c) return RES_FAIL; if (v) *v = c->verify; return RES_OK; }
SSF("_ZN2nn3ssl10Connection9SetOptionENS1_10OptionTypeEb", h_ssl_CnOpt, void *self, int t, unsigned on) {
    ONLINE_CONN(self); if (!c) return RES_FAIL;
    if (t >= 0 && t < 32) c->opts = (on & 1) ? c->opts | 1u << t : c->opts & ~(1u << t);
    return RES_OK;
}
SSF("_ZN2nn3ssl10Connection9GetOptionEPbNS1_10OptionTypeE", h_ssl_CnGetOpt, void *self, uint8_t *on, int t) { ONLINE_CONN(self); if (!c) return RES_FAIL; if (on) *on = t >= 0 && t < 32 && c->opts >> t & 1; return RES_OK; }
SSF("_ZN2nn3ssl10Connection19SetServerCertBufferEPcj", h_ssl_CnCertBuf, void *self, char *b, unsigned n) { (void)b; (void)n; ONLINE_CONN(self); return c ? RES_OK : RES_FAIL; }
SSF("_ZN2nn3ssl10Connection12GetLastErrorEPNS_6ResultE", h_ssl_CnLastErr, void *self, uint32_t *r) { ONLINE_CONN(self); if (r) *r = c ? c->last : RES_FAIL; return c ? RES_OK : RES_FAIL; }
SSF("_ZN2nn3ssl10Connection18GetVerifyCertErrorEPNS_6ResultE", h_ssl_CnVerifyErr, void *self, uint32_t *r) { ONLINE_CONN(self); if (r) *r = c ? RES_OK : RES_FAIL; return c ? RES_OK : RES_FAIL; }
SSF("_ZN2nn3ssl10Connection15GetConnectionIdEPm", h_ssl_CnId, void *self, uint64_t *id) { ONLINE_CONN(self); if (!c) return RES_FAIL; if (id) *id = c->id; return RES_OK; }
SSF("_ZN2nn3ssl10Connection12GetContextIdEPm", h_ssl_CnCtxId, void *self, uint64_t *id) { ONLINE_CONN(self); if (!c) return RES_FAIL; if (id) *id = 1; return RES_OK; }
SSF("_ZN2nn3ssl10Connection4PeekEPcPij", h_ssl_CnPeek, void *self, char *b, int *o, unsigned n) { ONLINE_CONN(self); if (!c) { if (o) *o = 0; return RES_FAIL; } return c->last = c_read(c, b, n, o, 0); }
SSF("_ZN2nn3ssl10Connection4ReadEPcPij", h_ssl_CnRead3, void *self, char *b, int *o, unsigned n) { ONLINE_CONN(self); if (!c) { if (o) *o = 0; return RES_FAIL; } return c->last = c_read(c, b, n, o, 1); }
SSF("_ZN2nn3ssl10Connection5WriteEPKcPij", h_ssl_CnWrite3, void *self, const char *b, int *o, unsigned n) { ONLINE_CONN(self); if (!c) { if (o) *o = 0; return RES_FAIL; } return c->last = c_write(c, b, n, o); }
SSF("_ZN2nn3ssl10Connection7PendingEPi", h_ssl_CnPending, void *self, int *n) { ONLINE_CONN(self); if (n) *n = c ? c_pending(c) : 0; return c ? RES_OK : RES_FAIL; }
SSF("_ZN2nn3ssl10Connection4PollEPNS1_9PollEventES3_j", h_ssl_CnPoll, void *self, uint32_t *o, uint32_t *i, unsigned t) { ONLINE_CONN(self); if (!c) { (void)i; (void)t; return RES_FAIL; } return c_poll(c, o, i, t); }
// ALPN (gRPC, Super Mario Party Jamboree's main+0xf9a4f4): the game offers "h2" and "http/1.1", and speaks HTTP/2 when the state is 1 or 3 (negotiated, selected) and the protocol "h2".
// State 0 = none negotiated
SSF("_ZN2nn3ssl10Connection16SetNextAlpnProtoEPKhj", h_ssl_CnSetAlpn, void *self, const uint8_t *p, unsigned n) {
    ONLINE_CONN(self); if (!c) return RES_FAIL;
    c->nalpn = p && n <= sizeof c->alpn ? n : 0;
    if (c->nalpn) memcpy(c->alpn, p, n);
    return RES_OK;
}
SSF("_ZN2nn3ssl10Connection16GetNextAlpnProtoEPNS1_14AlpnProtoStateEPhPjj", h_ssl_CnGetAlpn, void *self, int *state, uint8_t *out, uint32_t *n, unsigned cap) {
    ONLINE_CONN(self); if (!c) return RES_FAIL;
    CFArrayRef sel = NULL; char name[64] = "";
    if (c->tls && SSLCopyALPNProtocols(c->tls, &sel) == noErr && sel) {
        if (CFArrayGetCount(sel) > 0) CFStringGetCString(CFArrayGetValueAtIndex(sel, 0), name, sizeof name, kCFStringEncodingASCII);
        CFRelease(sel);
    }
    size_t len = strlen(name) < cap ? strlen(name) : cap;
    if (state) *state = len ? 1 : 0;
    if (out && len) memcpy(out, name, len);
    if (n) *n = (uint32_t)len;
    if (dmo_trace()) LOG("[online] trace TLS ALPN on socket %d: %s", c->gfd, len ? name : "none");
    return RES_OK;
}
// The rest of the connection's surface in Smash 13.0.5's sdk, so none of it falls through to the absent ssl service
SSF("_ZN2nn3ssl10Connection19GetSessionCacheModeEPNS1_16SessionCacheModeE", h_ssl_CnGetCache, void *self, int *m) { ONLINE_CONN(self); if (!c) return RES_FAIL; if (m) *m = c->cache; return RES_OK; }
SSF("_ZN2nn3ssl10Connection20SetRenegotiationModeENS1_17RenegotiationModeE", h_ssl_CnReneg, void *self, int m) { (void)m; ONLINE_CONN(self); return c ? RES_OK : RES_FAIL; }
SSF("_ZN2nn3ssl10Connection20GetRenegotiationModeEPNS1_17RenegotiationModeE", h_ssl_CnGetReneg, void *self, int *m) { ONLINE_CONN(self); if (!c) return RES_FAIL; if (m) *m = 0; return RES_OK; }
SSF("_ZN2nn3ssl10Connection17FlushSessionCacheEv", h_ssl_CnFlush, void *self) { ONLINE_CONN(self); return c ? RES_OK : RES_FAIL; }
SSF("_ZN2nn3ssl10Connection29GetNeededServerCertBufferSizeEPj", h_ssl_CnCertSize, void *self, uint32_t *n) { ONLINE_CONN(self); if (!c) return RES_FAIL; if (n) *n = 0; return RES_OK; }   // no certificate is handed back
SSF("_ZN2nn3ssl10Connection19GetVerifyCertErrorsEPNS_6ResultEPjS4_j", h_ssl_CnVerifyErrs, void *self, uint32_t *r, uint32_t *n, uint32_t *total, unsigned cap) {
    (void)r; (void)cap; ONLINE_CONN(self); if (!c) return RES_FAIL;
    if (n) *n = 0;
    if (total) *total = 0;
    return RES_OK;
}
SSF("_ZN2nn3ssl10Connection19GetServerCertDetailEPNS1_16ServerCertDetailEPKcj", h_ssl_CnCertDetail, void *self, void *d, const char *b, unsigned n) { (void)self; (void)d; (void)b; (void)n; return RES_FAIL; }   // no certificate kept to describe
// CipherInfo: the cipher's name (64 bytes) and the protocol version (8 bytes)
SSF("_ZN2nn3ssl10Connection13GetCipherInfoEPNS1_10CipherInfoE", h_ssl_CnCipher, void *self, char *info) {
    ONLINE_CONN(self); if (!c) return RES_FAIL;
    if (info) { memset(info, 0, 72); strlcpy(info, "ECDHE-RSA-AES128-GCM-SHA256", 64); strlcpy(info + 64, "TLSv1.2", 8); }
    return RES_OK;
}
HLE_FORCE("_ZN2nn3ssl10Connection7PendingEv", "ssl service: offline 0 (cut); online, bytes read ahead", int, h_ssl_CnPending0, void *self) { ONLINE_CONN(self); return c ? c_pending(c) : 0; }
// The older forms return the byte count, or -1 with the error kept for GetLastError
HLE_FORCE("_ZN2nn3ssl10Connection4ReadEPcj", "ssl service: offline returns -1 (cut); online, TLS to Nextendo", int, h_ssl_CnRead, void *self, char *b, unsigned n) {
    ONLINE_CONN(self); if (!c) return -1;
    int k = 0; c->last = c_read(c, b, n, &k, 1);
    return c->last ? -1 : k;
}
HLE_FORCE("_ZN2nn3ssl10Connection5WriteEPKcj", "ssl service: offline returns -1 (cut); online, TLS to Nextendo", int, h_ssl_CnWrite, void *self, const char *b, unsigned n) {
    ONLINE_CONN(self); if (!c) return -1;
    int k = 0; c->last = c_write(c, b, n, &k);
    return c->last ? -1 : k;
}
