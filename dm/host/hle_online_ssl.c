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

// ---------- connection ----------
static Result c_create(void *self) {
    conn_t *c = calloc(1, sizeof *c);
    if (!c) return R_ABORT;
    c->fd = c->gfd = -1; c->id = ++g_ids; c->verify = 3;
    objmap_put(self, c);
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
    return RES_OK;
}
static Result c_handshake(conn_t *c) {
    if (c->fd < 0) return R_NO_SOCKET;
    struct sockaddr_in peer = {0}; socklen_t len = sizeof peer; char ip[INET_ADDRSTRLEN] = "?";
    if (getpeername(c->fd, (struct sockaddr *)&peer, &len) == 0 && peer.sin_family == AF_INET) inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
    if (peer.sin_family != AF_INET || !dmo_is_server(peer.sin_addr.s_addr)) { LOG("[online] TLS to %s (%s) refused: not a Nextendo server", c->host, ip); return R_ABORT; }
    char name[256]; struct in_addr numeric;
    if (c->host[0] && inet_pton(AF_INET, c->host, &numeric) != 1) snprintf(name, sizeof name, "%s", c->host);
    else if (!dmo_host_for(peer.sin_addr.s_addr, name, sizeof name)) name[0] = 0;
    c->tls = SSLCreateContext(NULL, kSSLClientSide, kSSLStreamType);
    if (!c->tls) return R_ABORT;
    SSLSetIOFuncs(c->tls, io_read, io_write);
    SSLSetConnection(c->tls, (SSLConnectionRef)(intptr_t)c->fd);
    if (name[0]) SSLSetPeerDomainName(c->tls, name, strlen(name));
    SSLSetSessionOption(c->tls, kSSLSessionOptionBreakOnServerAuth, true);
    SSLSetProtocolVersionMin(c->tls, kTLSProtocol1);
    SSLSetProtocolVersionMax(c->tls, kTLSProtocol12);
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
static Result c_read(conn_t *c, char *b, uint32_t n, int *out, int consume) {
    if (!c->tls) return R_NO_SOCKET;
    for (;;) {
        if (c->npeek || !n) {
            size_t k = c->npeek < n ? c->npeek : n;
            memcpy(b, c->peek, k);
            if (consume) { memmove(c->peek, c->peek + k, c->npeek - k); c->npeek -= k; }
            if (out) *out = (int)k;
            return RES_OK;
        }
        if (c->eof) { if (out) *out = 0; return RES_OK; }
        size_t got = 0;
        OSStatus st = consume ? SSLRead(c->tls, b, n, &got) : SSLRead(c->tls, c->peek, sizeof c->peek, &got);
        if (got) {
            if (!consume) { c->npeek = got; continue; }
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
SSF("_ZN2nn3ssl14BuiltInManager28GetBuiltInCertificateBufSizeEPjPNS0_15CaCertificateIdEj", h_ssl_BiBufSize, uint32_t *o, void *ids, unsigned n) { (void)ids; (void)n; if (o) *o = 0; return RES_FAIL; }
SSF("_ZN2nn3ssl14BuiltInManager22GetBuiltInCertificatesEPPNS1_22BuiltInCertificateInfoEPhjPNS0_15CaCertificateIdEj", h_ssl_BiCerts, void **o, uint8_t *b, unsigned bs, void *ids, unsigned n) { (void)o; (void)b; (void)bs; (void)ids; (void)n; return RES_FAIL; }
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
