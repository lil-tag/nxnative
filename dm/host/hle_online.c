// Online play through Nextendo Network (DM_NEXTENDO=1), or a game's LAN mode with the same sockets (DM_LAN=1): the network status, sockets and name lookups that hle_net.c, hle_offline.c and hle_misc.c otherwise cut.
// Off by default; each of those stubs asks dm_online() first and comes here when it is on, so a front end turns online play on with one switch (settings.tsv) and nothing else.
// Sockets are the Mac's own, behind a table of small guest descriptors (select's fd_set holds 1024, and the host process may have more files open than that).
// Name lookups send Nintendo's server names to Nextendo's servers, with the rules Ryujinx-Nextendo uses, and resolve every other name normally.
// Numbering the SDK sees, as the bsd service answers it (Mush8's bsd.rs, played online with Mario Kart 8 Deluxe): Linux errno values, O_NONBLOCK 0x800; the
// levels, options, message flags and poll events are BSD's, the Mac's own. The SDK's addrinfo is BSD's too (canonname at +0x18, addr at +0x20: Smash's
// sdk, nn::socket::resolver::serializer::FreeAddrinfoContents), so the Mac's getaddrinfo results go to the game as they are and FreeAddrInfo frees them
#include "hle.h"
#include "online.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// DM_LAN=1: the same real sockets for a game's own LAN mode, on the local network only: no Nextendo, no sign-in, no name lookups. With DM_NEXTENDO=1 as well, Nextendo wins: it allows all of that too
int dm_lan(void) {
    static int on = -1;
    if (on < 0) { const char *l = getenv("DM_LAN"), *n = getenv("DM_NEXTENDO"); on = l && *l == '1' && !(n && *n == '1'); if (on) LOG("[online] LAN play is on"); }
    return on;
}
int dm_online(void) {
    static int on = -1;
    if (on < 0) { const char *e = getenv("DM_NEXTENDO"); int nx = e && *e == '1'; on = nx || dm_lan(); if (nx) LOG("[online] Nextendo online play is on"); }
    return on;
}

// ---------- errors: the SDK reads Linux errno values ----------
static __thread int t_err;
static int lx_errno(int e) {
    switch (e) {
    case EAGAIN: return 11; case EDEADLK: return 35; case ENAMETOOLONG: return 36; case ELOOP: return 40; case ENOTSOCK: return 88; case EDESTADDRREQ: return 89;
    case EMSGSIZE: return 90; case EPROTOTYPE: return 91; case ENOPROTOOPT: return 92; case EPROTONOSUPPORT: return 93; case ESOCKTNOSUPPORT: return 94;
    case EOPNOTSUPP: return 95; case EPFNOSUPPORT: return 96; case EAFNOSUPPORT: return 97; case EADDRINUSE: return 98; case EADDRNOTAVAIL: return 99;
    case ENETDOWN: return 100; case ENETUNREACH: return 101; case ENETRESET: return 102; case ECONNABORTED: return 103; case ECONNRESET: return 104;
    case ENOBUFS: return 105; case EISCONN: return 106; case ENOTCONN: return 107; case ESHUTDOWN: return 108; case ETOOMANYREFS: return 109;
    case ETIMEDOUT: return 110; case ECONNREFUSED: return 111; case EHOSTDOWN: return 112; case EHOSTUNREACH: return 113; case EALREADY: return 114;
    case EINPROGRESS: return 115; default: return e;   // 1-34 are the same on both
    }
}
// The bsd client also leaves the error in the C library's errno, which gRPC reads (Super Mario Party Jamboree)
static void set_err(int e) { t_err = e; int *g = dm_guest_errno(); if (g) *g = e; }
static long fail(void) { set_err(lx_errno(errno)); return -1; }
static const char *sa_str(const struct sockaddr_storage *ss, char *out, size_t n) {   // "a.b.c.d:port" for the trace
    if (ss->ss_family != AF_INET) { snprintf(out, n, "family %d", ss->ss_family); return out; }
    const struct sockaddr_in *in = (const struct sockaddr_in *)ss; char ip[INET_ADDRSTRLEN];
    snprintf(out, n, "%s:%u", inet_ntop(AF_INET, &in->sin_addr, ip, sizeof ip), ntohs(in->sin_port)); return out;
}
// DM_NEXTENDO_TRACE=1: every socket call with its arguments, result and errno (the guest's numbering), to see where a game's online step stops
int dmo_trace(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_NEXTENDO_TRACE"); v = e && *e == '1'; } return v; }
#define TRACE(...) do { if (dmo_trace()) LOG("[online] trace " __VA_ARGS__); } while (0)
#define TRACED(r, ...) ({ long r_t = (r); TRACE(__VA_ARGS__); TRACE("  → %ld (errno %d)", r_t, r_t < 0 ? t_err : 0); r_t; })
static long ok_or_fail(long r) { return r < 0 ? fail() : r; }
int dmo_errno(void) { return t_err; }
void dmo_set_errno(int e) { set_err(e); }

// A call that can wait (connect, accept, receive, send, poll, a name lookup) is a guest blocking boundary: the thread gives its core back while it waits
#define BLOCKING(expr) ({ dm_thread_native_wait(1); __typeof__(expr) r_ = (expr); int e_ = errno; dm_thread_native_wait(0); errno = e_; r_; })

// ---------- descriptors: guest 3..1023 → the Mac's ----------
enum { GFD_MAX = 1024, SW_O_NONBLOCK = 0x800, SW_AF_INET6 = 28 };
static _Atomic int g_fd[GFD_MAX];   // host descriptor + 1; 0 = free
static int g_evw[GFD_MAX];          // eventfd descriptors (dmo_eventfd): the pipe's write end + 1; 0 = a socket
static uint64_t g_evv[GFD_MAX];     // their counters
static uint8_t g_evsem[GFD_MAX];
static pthread_mutex_t g_ev_m = PTHREAD_MUTEX_INITIALIZER;
static int fd_host(int g) { return g >= 0 && g < GFD_MAX ? atomic_load(&g_fd[g]) - 1 : -1; }
static int fd_adopt(int h) {
    for (int g = 3; g < GFD_MAX; g++) { int z = 0; if (atomic_compare_exchange_strong(&g_fd[g], &z, h + 1)) return g; }
    close(h); set_err(24); return -1;   // EMFILE
}
#define HOST_OR_EBADF(g) int h = fd_host(g); if (h < 0) { set_err(9); return -1; }

// The guest's sockaddr is BSD's (sin_len, sin_family); a game may leave sin_len at 0, so it is set from the family
static socklen_t sa_in(struct sockaddr_storage *ss, const void *a, unsigned n) {
    memset(ss, 0, sizeof *ss);
    if (n > sizeof *ss) n = sizeof *ss;
    if (a) memcpy(ss, a, n);
    if (ss->ss_family == SW_AF_INET6) ss->ss_family = AF_INET6;
    socklen_t len = ss->ss_family == AF_INET ? sizeof(struct sockaddr_in) : ss->ss_family == AF_INET6 ? sizeof(struct sockaddr_in6) : n;
    ss->ss_len = (uint8_t)len;
    return len;
}
static void sa_out(void *a, unsigned *n, const struct sockaddr_storage *ss, socklen_t len) {
    if (!a || !n) return;
    unsigned c = *n < len ? *n : len;
    memcpy(a, ss, c);
    if (c > 1 && ss->ss_family == AF_INET6) ((uint8_t *)a)[1] = SW_AF_INET6;
    if (c) ((uint8_t *)a)[0] = 0;   // the bsd service leaves the length byte at 0 (Mush8)
    *n = len;
}
static int msg_flags(int f) { return f & (MSG_OOB | MSG_PEEK | MSG_DONTWAIT | MSG_WAITALL); }   // BSD values; the rest (no-signal) the Mac does per socket

int dmo_socket(int family, int type, int proto) {
    int nonblock = type & (SW_O_NONBLOCK | 0x20000000);   // SOCK_NONBLOCK in either numbering
    if (family == SW_AF_INET6) family = AF_INET6;
    int h = socket(family, type & 0xF, proto);
    if (h < 0) return (int)fail();
    int one = 1;
    setsockopt(h, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    if ((type & 0xF) == SOCK_DGRAM) setsockopt(h, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);   // two games on one Mac can share a port (Mush8)
    if (nonblock) fcntl(h, F_SETFL, fcntl(h, F_GETFL) | O_NONBLOCK);
    return (int)TRACED(fd_adopt(h), "socket family %d type %#x proto %d", family, type, proto);
}
// The game's code on the way to a call (frame pointers; the first few frames in a game module), for the trace
static void trace_callers(const char *what) {
    void **fp = __builtin_frame_address(0); char line[512]; int o = snprintf(line, sizeof line, "%s called from", what), found = 0;
    for (int i = 0; i < 24 && fp && !((uintptr_t)fp & 7) && found < 6; i++) {
        void *ra = fp[1]; module_t *m = module_of(ra);
        if (m && o < (int)sizeof line - 40) { o += snprintf(line + o, sizeof line - o, " %s+%#llx", m->name, (unsigned long long)((uint8_t *)ra - m->base)); found++; }
        // a game frame without a frame pointer leaves anything here (Jamboree's gRPC close, main+0xb63e38: 0xc21db233bb010210): stop at one that is no stack address above this one
        void **up = fp[0]; if (up <= fp || (uintptr_t)up - (uintptr_t)fp > (64u << 20)) break; fp = up;
    }
    LOG("[online] trace %s", line);
}
// UDP sockets parked across Pia's close-and-rebind (Ryujinx-Nextendo's NextendoUdpPortKeeper): Pia checks the NAT on one UDP socket, closes it and binds
// a new one to the same port for the match. The NAT mapping the check server saw belongs to the closed socket, so the address the matchmaking server hands
// the peer was dead, and Smash's quickplay ended in a communication error (2618-0006). A bound, unconnected UDP socket the game closes is held for
// PARK_MS instead, and a bind of a new UDP socket to the same port takes it back. At most PARK_MAX ports; anything else closes as before
enum { PARK_MAX = 8, PARK_MS = 5000 };
static struct { int port, fd; uint64_t until; } g_park[PARK_MAX];
static pthread_mutex_t g_park_m = PTHREAD_MUTEX_INITIALIZER;
static uint64_t park_now(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000000; }
static void park_expire(uint64_t now) {   // with g_park_m held
    for (int i = 0; i < PARK_MAX; i++) if (g_park[i].port && g_park[i].until <= now) { close(g_park[i].fd); TRACE("released parked udp/%d", g_park[i].port); g_park[i].port = 0; }
}
static int udp_port(int h) {   // the local port of a bound, unconnected IPv4 UDP socket, else 0
    int type = 0; socklen_t l = sizeof type; struct sockaddr_in in; socklen_t il = sizeof in; struct sockaddr_storage peer; socklen_t pl = sizeof peer;
    if (getsockopt(h, SOL_SOCKET, SO_TYPE, &type, &l) || type != SOCK_DGRAM) return 0;
    if (!getpeername(h, (struct sockaddr *)&peer, &pl)) return 0;   // connected: a real teardown
    if (getsockname(h, (struct sockaddr *)&in, &il) || in.sin_family != AF_INET) return 0;
    return ntohs(in.sin_port);
}
static int park(int h, int port) {
    pthread_mutex_lock(&g_park_m);
    uint64_t now = park_now(); park_expire(now);
    int k = -1;
    for (int i = 0; i < PARK_MAX && k < 0; i++) if (g_park[i].port == port) { close(g_park[i].fd); k = i; }   // the newer socket carries the mapping in use
    for (int i = 0; i < PARK_MAX && k < 0; i++) if (!g_park[i].port) k = i;
    if (k >= 0) { g_park[k].port = port; g_park[k].fd = h; g_park[k].until = now + PARK_MS; }
    pthread_mutex_unlock(&g_park_m);
    return k >= 0;
}
static int unpark(int port) {   // the host socket parked on this port, or -1
    int fd = -1;
    pthread_mutex_lock(&g_park_m);
    park_expire(park_now());
    for (int i = 0; i < PARK_MAX; i++) if (g_park[i].port == port) { fd = g_park[i].fd; g_park[i].port = 0; break; }
    pthread_mutex_unlock(&g_park_m);
    return fd;
}
int dmo_close(int g) {
    HOST_OR_EBADF(g);
    if (dmo_trace()) { char w[32]; snprintf(w, sizeof w, "close %d", g); trace_callers(w); }
    if (g_evw[g]) { pthread_mutex_lock(&g_ev_m); close(g_evw[g] - 1); g_evw[g] = 0; atomic_store(&g_fd[g], 0); pthread_mutex_unlock(&g_ev_m); return (int)ok_or_fail(close(h)); }   // an eventfd
    atomic_store(&g_fd[g], 0);
    int port = udp_port(h);
    if (port && park(h, port)) { TRACE("parked udp/%d to keep its NAT mapping", port); return 0; }
    return (int)ok_or_fail(close(h));
}
int dmo_shutdown(int g, int how) { HOST_OR_EBADF(g); return (int)ok_or_fail(shutdown(h, how)); }
int dmo_bind(int g, const void *a, unsigned n) {
    HOST_OR_EBADF(g); struct sockaddr_storage ss; socklen_t len = sa_in(&ss, a, n); char w[64];
    int type = 0; socklen_t tl = sizeof type; unsigned port = ss.ss_family == AF_INET ? ntohs(((struct sockaddr_in *)&ss)->sin_port) : 0;
    int kept = port && !getsockopt(h, SOL_SOCKET, SO_TYPE, &type, &tl) && type == SOCK_DGRAM ? unpark((int)port) : -1;
    if (kept >= 0) {   // the parked socket, already bound there, in place of this one; it takes this one's blocking mode
        int fl = fcntl(kept, F_GETFL);
        if (fl >= 0) fcntl(kept, F_SETFL, (fl & ~O_NONBLOCK) | (fcntl(h, F_GETFL) & O_NONBLOCK));
        atomic_store(&g_fd[g], kept + 1); close(h);
        TRACE("bind %d to %s: the parked udp/%u socket, its NAT mapping kept", g, sa_str(&ss, w, sizeof w), port);
        return 0;
    }
    return (int)TRACED(ok_or_fail(bind(h, (struct sockaddr *)&ss, len)), "bind %d to %s", g, sa_str(&ss, w, sizeof w));
}
int dmo_connect(int g, const void *a, unsigned n) {
    HOST_OR_EBADF(g); struct sockaddr_storage ss; socklen_t len = sa_in(&ss, a, n);
    if (ss.ss_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)&ss; char ip[INET_ADDRSTRLEN];
        LOG("[online] connect %d to %s:%u", g, inet_ntop(AF_INET, &in->sin_addr, ip, sizeof ip), ntohs(in->sin_port));
    } else if (ss.ss_family == AF_INET6) {
        const struct sockaddr_in6 *in = (const struct sockaddr_in6 *)&ss; char ip[INET6_ADDRSTRLEN];
        LOG("[online] connect %d to [%s]:%u", g, inet_ntop(AF_INET6, &in->sin6_addr, ip, sizeof ip), ntohs(in->sin6_port));
    }
    return (int)TRACED(ok_or_fail(BLOCKING(connect(h, (struct sockaddr *)&ss, len))), "connect %d", g);
}
int dmo_listen(int g, int backlog) { HOST_OR_EBADF(g); return (int)ok_or_fail(listen(h, backlog)); }
int dmo_accept(int g, void *a, unsigned *n) {
    HOST_OR_EBADF(g); struct sockaddr_storage ss; socklen_t len = sizeof ss;
    int c = BLOCKING(accept(h, (struct sockaddr *)&ss, &len));
    if (c < 0) return (int)fail();
    int one = 1; setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    sa_out(a, n, &ss, len);
    return fd_adopt(c);
}
int dmo_sockname(int g, void *a, unsigned *n, int peer) {
    HOST_OR_EBADF(g); struct sockaddr_storage ss; socklen_t len = sizeof ss;
    if ((peer ? getpeername(h, (struct sockaddr *)&ss, &len) : getsockname(h, (struct sockaddr *)&ss, &len)) < 0) return (int)TRACED(fail(), "%s %d", peer ? "getpeername" : "getsockname", g);
    TRACE("%s %d → 0", peer ? "getpeername" : "getsockname", g);
    sa_out(a, n, &ss, len);
    return 0;
}
// Timeouts are the guest's {i64 seconds, i64 microseconds}; the Mac's timeval keeps microseconds in an int
static int is_timeo(int level, int opt) { return level == SOL_SOCKET && (opt == SO_RCVTIMEO || opt == SO_SNDTIMEO); }
int dmo_getsockopt(int g, int level, int opt, void *v, unsigned *n) {
    HOST_OR_EBADF(g);
    if (is_timeo(level, opt) && v && n && *n >= 16) {
        struct timeval tv; socklen_t l = sizeof tv;
        if (getsockopt(h, level, opt, &tv, &l) < 0) return (int)fail();
        int64_t *o = v; o[0] = tv.tv_sec; o[1] = tv.tv_usec; *n = 16; return 0;
    }
    socklen_t l = n ? *n : 0;
    if (v) dm_guest_write_prepare(v, l);
    if (getsockopt(h, level, opt, v, &l) < 0) return (int)TRACED(fail(), "getsockopt %d level %#x opt %#x", g, level, opt);
    if (level == SOL_SOCKET && opt == SO_ERROR && v && l >= 4) *(int *)v = lx_errno(*(int *)v);
    if (n) *n = l;
    TRACE("getsockopt %d level %#x opt %#x → 0, value %d", g, level, opt, v && l >= 4 ? *(int *)v : -1);
    return 0;
}
int dmo_setsockopt(int g, int level, int opt, const void *v, unsigned n) {
    HOST_OR_EBADF(g);
    if (is_timeo(level, opt) && v && n >= 16) {
        const int64_t *i = v; struct timeval tv = {(time_t)i[0], (suseconds_t)i[1]};
        return (int)ok_or_fail(setsockopt(h, level, opt, &tv, sizeof tv));
    }
    return (int)TRACED(ok_or_fail(setsockopt(h, level, opt, v, n)), "setsockopt %d level %#x opt %#x value %d", g, level, opt, v && n >= 4 ? *(const int *)v : -1);
}
int dmo_fcntl(int g, int cmd, int arg) {
    HOST_OR_EBADF(g);
    if (cmd == F_GETFL) { int fl = fcntl(h, F_GETFL); return (int)TRACED(fl < 0 ? fail() : (fl & O_NONBLOCK ? SW_O_NONBLOCK : 0), "fcntl %d F_GETFL", g); }
    if (cmd == F_SETFL) { int fl = fcntl(h, F_GETFL) & ~O_NONBLOCK; return (int)TRACED(ok_or_fail(fcntl(h, F_SETFL, fl | (arg & SW_O_NONBLOCK ? O_NONBLOCK : 0))), "fcntl %d F_SETFL %#x", g, arg); }
    TRACE("fcntl %d cmd %d arg %#x: not handled, → 0", g, cmd, arg);
    return 0;
}
long dmo_recvfrom(int g, void *b, unsigned long n, int f, void *a, unsigned *al) {
    HOST_OR_EBADF(g); struct sockaddr_storage ss; socklen_t len = sizeof ss;
    dm_guest_write_prepare(b, n);   // a write-protected page (graphics tracking) would fail the system call with EFAULT
    long r = BLOCKING(recvfrom(h, b, n, msg_flags(f), a ? (struct sockaddr *)&ss : NULL, a ? &len : NULL));
    if (r < 0) return TRACED(fail(), "recv %d n %lu flags %#x", g, n, f);
    char w[64]; if (dmo_trace()) { if (a) sa_str(&ss, w, sizeof w); else strlcpy(w, "connected peer", sizeof w); }
    if (a) sa_out(a, al, &ss, len);
    TRACE("recv %d n %lu flags %#x → %ld from %s", g, n, f, r, w);
    return r;
}
long dmo_sendto(int g, const void *b, unsigned long n, int f, const void *a, unsigned al) {
    HOST_OR_EBADF(g);
    if (!a) return TRACED(ok_or_fail(BLOCKING(send(h, b, n, msg_flags(f)))), "send %d n %lu flags %#x", g, n, f);
    struct sockaddr_storage ss; socklen_t len = sa_in(&ss, a, al); char w[64];
    // A console can send to 255.255.255.255; the Mac answers EHOSTUNREACH. The adapter's own broadcast address reaches the same machines
    struct sockaddr_in *in = (struct sockaddr_in *)&ss;
    if (ss.ss_family == AF_INET && in->sin_addr.s_addr == INADDR_BROADCAST) { uint32_t m, me = dmo_ip(&m); if (me) in->sin_addr.s_addr = me | ~m; }
    return TRACED(ok_or_fail(BLOCKING(sendto(h, b, n, msg_flags(f), (struct sockaddr *)&ss, len))), "sendto %d n %lu flags %#x to %s", g, n, f, sa_str(&ss, w, sizeof w));
}
// sendmsg / recvmsg (gRPC's TCP reads and writes): the SDK's msghdr is BSD's, the Mac's (Jamboree's main+0xb36324: iovlen at +0x18, controllen at +0x28, flags at +0x2c); only the address is translated
long dmo_sendmsg(int g, const void *msg, int f) {
    HOST_OR_EBADF(g); struct msghdr m = *(const struct msghdr *)msg; struct sockaddr_storage ss;
    if (m.msg_name) { m.msg_namelen = sa_in(&ss, m.msg_name, m.msg_namelen); m.msg_name = &ss; }
    return TRACED(ok_or_fail(BLOCKING(sendmsg(h, &m, msg_flags(f)))), "sendmsg %d iov %d flags %#x", g, (int)m.msg_iovlen, f);
}
long dmo_recvmsg(int g, void *msg, int f) {
    HOST_OR_EBADF(g); struct msghdr *gm = msg, m = *gm; struct sockaddr_storage ss;
    if (m.msg_name) { m.msg_name = &ss; m.msg_namelen = sizeof ss; }
    long r = BLOCKING(recvmsg(h, &m, msg_flags(f)));
    if (r < 0) return TRACED(fail(), "recvmsg %d flags %#x", g, f);
    if (gm->msg_name) sa_out(gm->msg_name, &gm->msg_namelen, &ss, m.msg_namelen);
    gm->msg_controllen = m.msg_controllen; gm->msg_flags = m.msg_flags;
    TRACE("recvmsg %d flags %#x → %ld", g, f, r);
    return r;
}

// eventfd (gRPC's poller wakeup, Jamboree's main+0xb3a080): a counter whose descriptor polls readable while it is above 0. The Mac has none, so the descriptor is a pipe's read end
// holding one byte while the counter is above 0. Always nonblocking, as Ryujinx-Nextendo has it: a read at 0 fails with EAGAIN. Flags: 1 semaphore (reads take 1), 4 nonblocking
int dmo_eventfd(uint64_t v, int flags) {
    int p[2]; if (pipe(p)) return (int)fail();
    fcntl(p[0], F_SETFL, O_NONBLOCK); fcntl(p[1], F_SETFL, O_NONBLOCK);
    pthread_mutex_lock(&g_ev_m);
    int g = fd_adopt(p[0]);
    if (g >= 0) { g_evw[g] = p[1] + 1; g_evv[g] = v; g_evsem[g] = flags & 1; if (v) (void)!write(p[1], "", 1); } else close(p[1]);
    pthread_mutex_unlock(&g_ev_m);
    TRACE("eventfd %llu flags %#x → %d", (unsigned long long)v, flags, g);
    return g;
}
int dmo_eventfd_read(int g, uint64_t *v) {
    HOST_OR_EBADF(g);
    pthread_mutex_lock(&g_ev_m);
    int e = !g_evw[g] ? 22 : !g_evv[g] ? 11 : 0;   // EINVAL, EAGAIN
    if (!e) {
        uint64_t n = g_evsem[g] ? 1 : g_evv[g];
        if (!(g_evv[g] -= n)) { char b[16]; while (read(h, b, sizeof b) > 0) {} }
        if (v) *v = n;
    }
    pthread_mutex_unlock(&g_ev_m);
    if (e) { set_err(e); return -1; }
    return 0;
}
int dmo_eventfd_write(int g, uint64_t v) {
    HOST_OR_EBADF(g); (void)h;
    pthread_mutex_lock(&g_ev_m);
    int e = !g_evw[g] || v == UINT64_MAX ? 22 : g_evv[g] + v < g_evv[g] || g_evv[g] + v == UINT64_MAX ? 11 : 0;
    if (!e) { if (!g_evv[g] && v) (void)!write(g_evw[g] - 1, "", 1); g_evv[g] += v; }
    pthread_mutex_unlock(&g_ev_m);
    if (e) { set_err(e); return -1; }
    return 0;
}

// poll: the guest's pollfd is the Mac's; descriptors are translated, a negative one is ignored and an unknown one reports POLLNVAL, as on Horizon
typedef struct { int32_t fd; int16_t events, revents; } sw_pollfd;
int dmo_poll(void *fds, uint64_t n, int ms) {
    sw_pollfd *p = fds; struct pollfd stack[64], *q = n <= 64 ? stack : calloc(n, sizeof *q);
    if (!q) { set_err(12); return -1; }
    int bad = 0;
    for (uint64_t i = 0; i < n; i++) {
        q[i].fd = p[i].fd < 0 ? -1 : fd_host(p[i].fd); q[i].events = p[i].events; q[i].revents = 0;
        if (p[i].fd >= 0 && q[i].fd < 0) bad++;
    }
    int r = BLOCKING(poll(q, (nfds_t)n, bad ? 0 : ms));   // the Mac ignores the -1 entries
    if (r >= 0) for (uint64_t i = 0; i < n; i++) {
        p[i].revents = p[i].fd >= 0 && q[i].fd < 0 ? POLLNVAL : q[i].revents;
        if (p[i].fd >= 0 && q[i].fd < 0) r++;
    }
    int e = errno;
    if (q != stack) free(q);
    errno = e;
    if (dmo_trace()) for (uint64_t i = 0; i < n && i < 8; i++) TRACE("poll fd %d events %#x → revents %#x (timeout %d ms, result %d)", p[i].fd, p[i].events, r >= 0 ? p[i].revents : 0, ms, r);
    return r < 0 ? (int)fail() : r;
}
// select through poll: the guest's fd_set is 1024 bits, its timeval {i64 seconds, i64 microseconds} or NULL to wait for good
int dmo_select(int nfds, uint64_t *r, uint64_t *w, uint64_t *e, const int64_t *tv) {
    if (nfds > GFD_MAX) nfds = GFD_MAX;
    sw_pollfd p[GFD_MAX]; int n = 0;
    uint64_t *sets[3] = {r, w, e}; const int16_t ev[3] = {POLLIN, POLLOUT, POLLPRI};
    for (int fd = 0; fd < nfds; fd++) {
        int16_t m = 0;
        for (int k = 0; k < 3; k++) if (sets[k] && sets[k][fd >> 6] >> (fd & 63) & 1) m |= ev[k];
        if (m) p[n++] = (sw_pollfd){fd, m, 0};
    }
    int got = dmo_poll(p, (uint64_t)n, tv ? (int)(tv[0] * 1000 + tv[1] / 1000) : -1);
    if (got < 0) return -1;
    for (int k = 0; k < 3; k++) if (sets[k]) memset(sets[k], 0, 128);
    int count = 0;
    for (int i = 0; i < n; i++) {
        int16_t re = p[i].revents, fd = (int16_t)p[i].fd;
        int16_t hit[3] = {re & (POLLIN | POLLERR | POLLHUP | POLLNVAL), re & POLLOUT, re & POLLPRI};
        for (int k = 0; k < 3; k++) if (sets[k] && hit[k] && p[i].events & ev[k]) { sets[k][fd >> 6] |= 1ull << (fd & 63); count++; }
    }
    return count;
}

// ---------- Nextendo's servers: where Nintendo's server names go ----------
// The rules and their order are Ryujinx-Nextendo's (DnsMitmResolver._builtinRedirects): most specific first. The addresses are Mush8's, which Ryujinx-Nextendo's
// official builds set through NEXTENDO_SERVER_IP / NEXTENDO_NAT_IP; DM_NEXTENDO_SERVER and DM_NEXTENDO_NAT override them here
static const char *server_ip(int nat) {
    const char *e = getenv(nat ? "DM_NEXTENDO_NAT" : "DM_NEXTENDO_SERVER");
    return e && *e ? e : nat ? "164.132.111.120" : "51.178.29.194";
}
static int glob_match(const char *pat, const char *s) {   // one '*' at most, as the rules have
    const char *star = strchr(pat, '*');
    if (!star) return !strcasecmp(pat, s);
    size_t pre = (size_t)(star - pat), post = strlen(star + 1), n = strlen(s);
    return n >= pre + post && !strncasecmp(s, pat, pre) && !strcasecmp(s + n - post, star + 1);
}
static const struct { const char *pat; int nat; } k_rules[] = {
    {"nncs2-*.n.n.srv.nintendo.net", 1},
    {"g2b309e01-lp1.s.n.srv.nintendo.net", 1},   // Mario Kart 8 Deluxe's game server lives beside the NAT responder
    {"g2df33d01-lp1.p.srv.nintendo.net", 1},     // Splatoon 2's rendezvous point
    {"*.nintendo.net", 0}, {"*.nintendo.com", 0}, {"*.nintendowifi.net", 0}, {"*.nintendo.co.jp", 0},
    {"demonware.net", 0}, {"*.demonware.net", 0},
    {"nswitch.f13.illfonic.com", 0}, {"spring18.gosredirector.ea.com", 0}, {"pvzgw3prodapp-*.ea.com", 0},
    {"public-ubiservices.ubi.com", 0}, {"public-ws-ubiservices.ubi.com", 0}, {"jd.ubisoft.com", 0}, {"phonescoring.jd.ubisoft.com", 0}, {"v2.phonescoring.jd.ubisoft.com", 0},
    {"api.braincloudservers.com", 0}, {"na.ultimatechickenhorseserver.com", 0}, {"eu.ultimatechickenhorseserver.com", 0}, {"ap.ultimatechickenhorseserver.com", 0},
};
const char *dmo_redirect(const char *host) {
    for (size_t i = 0; host && i < sizeof k_rules / sizeof *k_rules; i++) if (glob_match(k_rules[i].pat, host)) return server_ip(k_rules[i].nat);
    return NULL;
}
int dmo_is_server(uint32_t ip) {   // network byte order
    uint32_t a = 0, b = 0;
    inet_pton(AF_INET, server_ip(0), &a); inet_pton(AF_INET, server_ip(1), &b);
    return ip && (ip == a || ip == b);
}
// Addresses of names that were not Nintendo's (looked up on the internet as they are): TLS to them is refused, since Nextendo's certificates are accepted unchecked.
// Any other address came from Nextendo's own replies: Mario Tennis Aces' game server (144.202.45.50:60010), which Nextendo's login hands out, is not one of its two addresses
enum { PUBLIC_MAX = 64 };
static uint32_t g_public[PUBLIC_MAX]; static unsigned g_npublic;
static pthread_mutex_t g_public_m = PTHREAD_MUTEX_INITIALIZER;
static void remember_public(const struct addrinfo *ai) {
    pthread_mutex_lock(&g_public_m);
    for (; ai; ai = ai->ai_next) if (ai->ai_family == AF_INET) g_public[g_npublic++ % PUBLIC_MAX] = ((const struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr;
    pthread_mutex_unlock(&g_public_m);
}
int dmo_is_trusted(uint32_t ip) {
    if (dmo_is_server(ip)) return 1;
    int pub = 0;
    pthread_mutex_lock(&g_public_m);
    for (unsigned i = 0; i < PUBLIC_MAX; i++) pub |= ip == g_public[i];
    pthread_mutex_unlock(&g_public_m);
    return ip && !pub;
}
// The name a game last looked up for each of Nextendo's addresses: TLS sends it as the server name (SNI), which picks the service behind Nextendo's shared port 443
static struct { uint32_t ip; char name[256]; } g_last[2];
static pthread_mutex_t g_last_m = PTHREAD_MUTEX_INITIALIZER;
static void remember(const char *ip, const char *name) {
    uint32_t a = 0; inet_pton(AF_INET, ip, &a);
    pthread_mutex_lock(&g_last_m);
    int k = g_last[0].ip == a || !g_last[0].ip ? 0 : 1;
    g_last[k].ip = a; snprintf(g_last[k].name, sizeof g_last[k].name, "%s", name);
    pthread_mutex_unlock(&g_last_m);
}
int dmo_host_for(uint32_t ip, char *out, size_t cap) {
    int found = 0;
    pthread_mutex_lock(&g_last_m);
    for (int k = 0; k < 2; k++) if (g_last[k].ip == ip && g_last[k].name[0]) { snprintf(out, cap, "%s", g_last[k].name); found = 1; }
    pthread_mutex_unlock(&g_last_m);
    return found;
}
int dmo_host_fd(int g) { return fd_host(g); }
// nsd's server name templates put the environment where the '%' is ("g2b309e01-%.s.n.srv.nintendo.net"); the account server is Nextendo's BAAS (Mush8, nextendo.rs)
static void fill_env(char *out, size_t cap, const char *in) {
    size_t o = 0;
    for (; *in && o + 1 < cap; in++) {
        if (*in == '%' && o + 4 < cap) { memcpy(out + o, "lp1", 3); o += 3; } else out[o++] = *in;
    }
    out[o] = 0;
    if (!strcasecmp(out, "accounts.nintendo.com") || !strcasecmp(out, "api.accounts.nintendo.com")) snprintf(out, cap, "e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com");
}
Result dmo_nsd_resolve(char *out, const char *in) {   // Fqdn: char[256]
    fill_env(out, 256, in);
    return RES_OK;
}

// ---------- name lookups ----------
int dmo_getaddrinfo(const char *node, const char *service, const void *hints, void **res) {
    struct addrinfo h = {0};
    if (hints) memcpy(&h, hints, offsetof(struct addrinfo, ai_addrlen));   // flags, family, type, protocol
    h.ai_family = AF_INET;   // Nextendo is IPv4, and the Mac's AF_INET6 is not the guest's
    char name[256]; const char *ip = NULL;
    if (node) { fill_env(name, sizeof name, node); node = name; ip = dm_lan() ? NULL : dmo_redirect(name); }
    if (ip) { h.ai_flags |= AI_NUMERICHOST; remember(ip, name); }
    if (dm_lan()) h.ai_flags |= AI_NUMERICHOST;   // LAN play stays on the local network: a server name fails as it does offline
    int r = BLOCKING(getaddrinfo(ip ? ip : node, service, &h, (struct addrinfo **)res));
    if (node) LOG("[online] lookup %s: %s%s", node, r ? gai_strerror(r) : ip ? ip : "resolved", ip ? " (Nextendo)" : "");
    struct in_addr numeric;
    if (!r && !ip && node && inet_pton(AF_INET, node, &numeric) != 1) remember_public(*res);
    if (r) *res = NULL;
    return r;   // EAI_* values are BSD's on both
}
void dmo_freeaddrinfo(void *ai) { if (ai) freeaddrinfo(ai); }
// nn::socket::AddrInfo (the SDK's own type, GetAddrInfo's typed overload) keeps ai_addr at +0x18 and ai_canonname at +0x20, Linux's order; the libc
// addrinfo overload and nnsocketGetAddrInfo have the Mac's, canonname first. Smash's Pia reads the NAT check server's address through the typed one
// (main+0xba8d0): with the Mac's order it took the empty name for the address and crashed at matchmaking
static void ai_swap(struct addrinfo *ai) { for (; ai; ai = ai->ai_next) { void *t = ai->ai_canonname; ai->ai_canonname = (char *)ai->ai_addr; ai->ai_addr = t; } }
int dmo_getaddrinfo_nn(const char *node, const char *service, const void *hints, void **res) {
    int r = dmo_getaddrinfo(node, service, hints, res);
    if (!r) ai_swap(*res);
    return r;
}
void dmo_freeaddrinfo_nn(void *ai) { ai_swap(ai); dmo_freeaddrinfo(ai); }
// hostent has the same layout everywhere; one per thread, as gethostbyname's
void *dmo_gethostbyname(const char *node) {
    static __thread struct hostent he; static __thread char name[256]; static __thread struct in_addr addr[8]; static __thread char *list[9];
    struct addrinfo *ai = NULL;
    if (dmo_getaddrinfo(node, NULL, NULL, (void **)&ai) || !ai) { t_err = 2; return NULL; }   // h_errno HOST_NOT_FOUND
    snprintf(name, sizeof name, "%s", node);
    int n = 0;
    for (struct addrinfo *p = ai; p && n < 8; p = p->ai_next) if (p->ai_family == AF_INET) { addr[n] = ((struct sockaddr_in *)p->ai_addr)->sin_addr; list[n] = (char *)&addr[n]; n++; }
    list[n] = NULL;
    freeaddrinfo(ai);
    he = (struct hostent){.h_name = name, .h_aliases = &list[8], .h_addrtype = AF_INET, .h_length = 4, .h_addr_list = list};
    return &he;
}

// ---------- network status: the Mac's own address ----------
// The address the Mac reaches the internet from: a UDP socket "connected" to a public address sends nothing and reports the interface it would use
uint32_t dmo_ip(uint32_t *mask) {
    struct sockaddr_in to = {.sin_len = sizeof to, .sin_family = AF_INET, .sin_port = htons(53)}, me = {0};
    inet_pton(AF_INET, server_ip(0), &to.sin_addr);
    socklen_t len = sizeof me; int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0 || connect(s, (struct sockaddr *)&to, sizeof to) < 0 || getsockname(s, (struct sockaddr *)&me, &len) < 0) me.sin_addr.s_addr = 0;
    if (s >= 0) close(s);
    if (mask) {
        *mask = htonl(0xffffff00u);
        struct ifaddrs *ifs = NULL;
        if (!getifaddrs(&ifs)) {
            for (struct ifaddrs *i = ifs; i; i = i->ifa_next)
                if (i->ifa_addr && i->ifa_netmask && i->ifa_addr->sa_family == AF_INET && ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr == me.sin_addr.s_addr) {
                    *mask = ((struct sockaddr_in *)i->ifa_netmask)->sin_addr.s_addr; break;
                }
            freeifaddrs(ifs);
        }
    }
    return me.sin_addr.s_addr;   // network byte order, as in_addr
}
Result dmo_ip_config(uint32_t *ip, uint32_t *mask, uint32_t *gw, uint32_t *dns1, uint32_t *dns2) {
    uint32_t m, a = dmo_ip(&m);
    if (!a) return RES_FAIL;
    if (ip) *ip = a;
    if (mask) *mask = m;
    if (gw) *gw = (a & m) | htonl(1);   // shortcut: the subnet's first address; read the routing table if a game ever uses it
    if (dns1) inet_pton(AF_INET, "1.1.1.1", dns1);
    if (dns2) inet_pton(AF_INET, "8.8.8.8", dns2);
    return RES_OK;
}
