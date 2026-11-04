// Networking (out of scope: online play/friends/store/cloud saves): all hooked up as "network unavailable", the game takes the offline branch.
// Cuts: nifm reports no network; socket only hooks up Initialize (returns success, creates no session), the rest of socket/ssl/friends are hooked up as failures when they come into use.
#include "hle.h"
#include "online.h"
#include <stdatomic.h>

// SDK 20's form (Mario Kart World): SetAllocator stays in the SDK, this one would connect to bsd:s itself. Its Wireless Play and LAN mode ask before opening a socket
HLE_FORCE("_ZN2nn6socket8internal10InitializeEi", "connects to the bsd socket service; offline, creates no session (cut)", Result, h_socket_InitConc, int concurrency) { (void)concurrency; return RES_OK; }
HLE_FORCE("_ZN2nn6socket13IsInitializedEv", "whether the bsd socket service is connected: the host's sockets are there when online", unsigned, h_socket_IsInit, void) { return dm_online() ? 1 : 0; }
HLE_FORCE("_ZN2nn6socket10InitializeERKNS0_6ConfigE", "connects to the bsd socket service; offline, creates no session (cut)", Result, h_socket_Initialize, const void *cfg) { (void)cfg; return RES_OK; }
HLE_FORCE("_ZN2nn4nifm10InitializeEv", "connects to the nifm network management service; offline (cut)", Result, h_nifm_Initialize, void) { return RES_OK; }
// The request is asynchronous on Horizon: SubmitNetworkRequest only hands it to the nifm service, and IsNetworkRequestOnHold stays true until the service has evaluated it, so a query
// right after the submission finds it on hold. Pikmin 4 (Unreal Engine 4.26, main+0x203f3b0) relies on that: it starts its BCAT thread, whose Run (main+0x203f510) sets its status
// to 2, submits, waits while on hold and sets 6 (done, offline), and only then stores status 1 itself; answered at once, a thread that ran first finished before that store, the 1 stayed,
// and the boot state machine (main+0x1f2f764, state 7) waited for the status forever at the black loading screen (about 1 boot in 6, 3 in 4 with four at once). Pikmin 4 sleeps 10 ms
// between its queries, so a request that is on hold at the first query is all it needs.
// Each query is an IPC to the nifm service on Horizon, which evaluates the request as it serves them (offline, a rejection), so the request is on hold for the first query after
// the submission and settled at the next. The hold used to be 50 ms of host time: Hades' NetworkPollThread (main+0x27aaa0) polls with SleepThread(0) and cancels after 2,000
// queries, 2,000 IPCs on the Switch but 0.1 ms here, so it cancelled and submitted again forever (1.17 billion queries a minute, a host core). Its stamp, now_ns() | 1, also read
// as settled at once whenever the next query came within the same 42 ns clock tick (30% of Hades' submissions)
static _Atomic int g_nifm_hold;   // queries the pending request still answers "on hold" (0 = none, or settled)
HLE_FORCE("_ZN2nn4nifm20SubmitNetworkRequestEv", "offline: the request is on hold for the first query, then settles as unavailable", void, h_nifm_Submit, void) { atomic_store(&g_nifm_hold, 1); }
HLE_FORCE("_ZN2nn4nifm20CancelNetworkRequestEv", "offline: drops the pending request", void, h_nifm_Cancel, void) { atomic_store(&g_nifm_hold, 0); }
HLE_FORCE("_ZN2nn4nifm22IsNetworkRequestOnHoldEv", "offline: on hold for the first query after the submission", int, h_nifm_OnHold, void) {
    int h = atomic_load(&g_nifm_hold);
    while (h > 0 && !atomic_compare_exchange_weak(&g_nifm_hold, &h, h - 1)) {}
    return h > 0;
}
HLE_FORCE("_ZN2nn4nifm18IsNetworkAvailableEv", "offline: the network is always unavailable", int, h_nifm_Available, void) { if (dm_online()) return 1; return 0; }
// Returns bool (`and w0, w0, #1`, Metroid Dread's sdk+0x336210): whether the system's network screen resolved the error
HLE_FORCE("_ZN2nn4nifm31HandleNetworkRequestErrorResultEv", "offline: not resolved, shows no system network screen", unsigned, h_nifm_HandleErr, void) { return 0; }
// The request's result, after the system's network screen when it failed. Mario Tennis Aces goes online only on success (main+0x15de98); left to the SDK it asked the
// absent nifm service, failed, and the online menu returned to itself
HLE_FORCE("_ZN2nn4nifm26HandleNetworkRequestResultEv", "offline: the request failed, no system network screen; online, it succeeded", Result, h_nifm_HandleRes, void) { return dm_online() ? RES_OK : RES_FAIL; }
HLE_FORCE("_ZN2nn4nifm26GetCurrentPrimaryIpAddressEPNS_6socket6InAddrE", "offline: no IP", Result, h_nifm_GetIp, uint32_t *a) { if (dm_online()) return dmo_ip_config(a, 0, 0, 0, 0); if (a) *a = 0; return RES_FAIL; }
HLE_FORCE("_ZN2nn4nifm22GetCurrentProxySettingEPNS0_12ProxySettingE", "offline: no proxy setting; online, proxy off", Result, h_nifm_GetProxy, void *p) {
    if (!dm_online()) return RES_FAIL;
    if (p) memset(p, 0, 0xaa);   // ProxySetting: enabled, port, server[100], authentication {enabled, user[32], password[32]}; all off, as Ryujinx answers
    return RES_OK;
}
HLE_FORCE("nnsslInitialize", "connects to the ssl service; the network library's global init needs it to succeed, offline creates no session (cut)", Result, h_ssl_Initialize, void) { return RES_OK; }
HLE_FORCE("nnsslFinalize", "pairs with nnsslInitialize (cut)", Result, h_ssl_Finalize, void) { return RES_OK; }

// ---------- socket: offline, all calls that contact the service return -1 in BSD convention, error code ENETDOWN ----------
// Not cut: Inet* / nnsocketInet* byte order and address text conversion (pure computation).
#define SK_ENETDOWN 50                  // Horizon sockets use FreeBSD error codes
static __thread int t_skerr = SK_ENETDOWN;
static int sk_fail(void) { t_skerr = SK_ENETDOWN; return -1; }
#define SKF(m, cname, ...) HLE_FORCE(m, "bsd socket service (cut): returns -1/ENETDOWN", int, cname, __VA_ARGS__)
// Poll / Select with nothing to wait on: the host creates no socket, so a game's descriptors are all -1, or it passes none. POSIX and Horizon's bsd service ignore negative
// descriptors and wait out the timeout, returning 0. Failing at once made Dark Souls Remastered's network thread ("NexusRevolution Socket", main+0x92c7c4) spin a core at 100%
// on Poll(fds, 0, 33 ms), 11,000 calls a second. An endless wait on nothing never returns on hardware; here it returns 0 after 100 ms: a thread in a host hook pauses for a garbage
// collector (IL2CPP's, hle_thread.c) only once the hook returns, and the caller can still see a shutdown
void dm_guest_sleep_ns(int64_t ns);
static int sk_idle(int64_t ms) { dm_guest_sleep_ns((ms < 0 ? 100 : ms) * 1000000); return 0; }
typedef struct { int32_t fd; int16_t events, revents; } sk_pollfd;
int dm_sk_poll(void *fds, uint64_t n, int ms) {   // also hle_offline.c's plain pollfd form
    if (dm_online()) return dmo_poll(fds, n, ms);
    sk_pollfd *p = fds;
    for (uint64_t i = 0; i < n; i++) if (p[i].fd >= 0) return sk_fail();
    for (uint64_t i = 0; i < n; i++) p[i].revents = 0;
    return sk_idle(ms);
}
int dm_sk_select(int nfds, const uint64_t *r, const uint64_t *w, const uint64_t *e, const int64_t *tv) {   // fd_set: 1024 bits; timeval: {i64 seconds, i64 microseconds}
    if (dm_online()) return dmo_select(nfds, (uint64_t *)r, (uint64_t *)w, (uint64_t *)e, tv);
    const uint64_t *sets[3] = {r, w, e};
    for (int k = 0; k < 3; k++) for (int fd = 0; sets[k] && fd < nfds && fd < 1024; fd++) if (sets[k][fd >> 6] >> (fd & 63) & 1) return sk_fail();
    return sk_idle(tv ? tv[0] * 1000 + tv[1] / 1000 : -1);
}
SKF("_ZN2nn6socket6SocketENS0_6FamilyENS0_4TypeENS0_8ProtocolE", h_sk_Socket, int a, int b, int c) { if (dm_online()) return dmo_socket(a, b, c); (void)a; (void)b; (void)c; return sk_fail(); }
SKF("_ZN2nn6socket4BindEiPKNS0_8SockAddrEj", h_sk_Bind, int s, const void *a, unsigned n) { if (dm_online()) return dmo_bind(s, a, n); (void)s; (void)a; (void)n; return sk_fail(); }
SKF("_ZN2nn6socket7ConnectEiPKNS0_8SockAddrEj", h_sk_Connect, int s, const void *a, unsigned n) { if (dm_online()) return dmo_connect(s, a, n); (void)s; (void)a; (void)n; return sk_fail(); }
SKF("_ZN2nn6socket6ListenEii", h_sk_Listen, int s, int n) { if (dm_online()) return dmo_listen(s, n); (void)s; (void)n; return sk_fail(); }
SKF("_ZN2nn6socket6AcceptEiPNS0_8SockAddrEPj", h_sk_Accept, int s, void *a, unsigned *n) { if (dm_online()) return dmo_accept(s, a, n); (void)s; (void)a; (void)n; return sk_fail(); }
SKF("_ZN2nn6socket5CloseEi", h_sk_Close, int s) { if (dm_online()) return dmo_close(s); (void)s; return sk_fail(); }
SKF("_ZN2nn6socket11GetPeerNameEiPNS0_8SockAddrEPj", h_sk_GetPeerName, int s, void *a, unsigned *n) { if (dm_online()) return dmo_sockname(s, a, n, 1); (void)s; (void)a; (void)n; return sk_fail(); }
// Older SDKs' forms (Mario Tennis Aces): the plain sockaddr overload, the same layout
SKF("_ZN2nn6socket11GetPeerNameEiP8sockaddrPj", h_sk_GetPeerName2, int s, void *a, unsigned *n) { if (dm_online()) return dmo_sockname(s, a, n, 1); (void)s; (void)a; (void)n; return sk_fail(); }
SKF("_ZN2nn6socket11GetSockNameEiPNS0_8SockAddrEPj", h_sk_GetSockName, int s, void *a, unsigned *n) { if (dm_online()) return dmo_sockname(s, a, n, 0); (void)s; (void)a; (void)n; return sk_fail(); }
SKF("_ZN2nn6socket10GetSockOptEiNS0_5LevelENS0_6OptionEPvPj", h_sk_GetSockOpt, int s, int l, int o, void *v, unsigned *n) { if (dm_online()) return dmo_getsockopt(s, l, o, v, n); (void)s; (void)l; (void)o; (void)v; (void)n; return sk_fail(); }
SKF("_ZN2nn6socket10SetSockOptEiNS0_5LevelENS0_6OptionEPKvj", h_sk_SetSockOpt, int s, int l, int o, const void *v, unsigned n) { if (dm_online()) return dmo_setsockopt(s, l, o, v, n); (void)s; (void)l; (void)o; (void)v; (void)n; return sk_fail(); }
SKF("_ZN2nn6socket5FcntlEiNS0_12FcntlCommandEz", h_sk_Fcntl, int s, int c, int arg) { if (dm_online()) return dmo_fcntl(s, c, arg); (void)s; (void)c; (void)arg; (void)s; (void)c; return sk_fail(); }
SKF("_ZN2nn6socket4PollEPNS0_6PollFdEmi", h_sk_Poll, void *f, unsigned long n, int t) { return dm_sk_poll(f, n, t); }
SKF("_ZN2nn6socket6SelectEiPNS0_5FdSetES2_S2_PNS0_7TimeValE", h_sk_Select, int n, void *r, void *w, void *e, void *t) { return dm_sk_select(n, r, w, e, t); }
// gRPC (Super Mario Party Jamboree's online) aborts at startup when its poller gets no eventfd: "No event engine could be initialized"
SKF("_ZN2nn6socket7EventFdEmNS0_12EventFdFlagsE", h_sk_EventFd, unsigned long v, int f) { if (dm_online()) return dmo_eventfd(v, f); (void)v; (void)f; return sk_fail(); }
SKF("_ZN2nn6socket11EventFdReadEiPm", h_sk_EventFdRead, int s, unsigned long *v) { if (dm_online()) return dmo_eventfd_read(s, (uint64_t *)v); (void)s; (void)v; return sk_fail(); }
SKF("_ZN2nn6socket12EventFdWriteEim", h_sk_EventFdWrite, int s, unsigned long v) { if (dm_online()) return dmo_eventfd_write(s, v); (void)s; (void)v; return sk_fail(); }
SKF("_ZN2nn6socket17ResolverGetOptionEPNS0_14ResolverOptionENS0_17ResolverOptionKeyE", h_sk_ResGet, void *o, int k) { if (dm_online()) return 0; (void)o; (void)k; return sk_fail(); }
SKF("_ZN2nn6socket17ResolverSetOptionERKNS0_14ResolverOptionE", h_sk_ResSet, const void *o) { if (dm_online()) return 0; (void)o; return sk_fail(); }
HLE_FORCE("_ZN2nn6socket4RecvEiPvmNS0_7MsgFlagE", "bsd socket service (cut)", long, h_sk_Recv, int s, void *b, unsigned long n, int f) { if (dm_online()) return dmo_recvfrom(s, b, n, f, 0, 0); (void)s; (void)b; (void)n; (void)f; return sk_fail(); }
HLE_FORCE("_ZN2nn6socket8RecvFromEiPvmNS0_7MsgFlagEPNS0_8SockAddrEPj", "bsd socket service (cut)", long, h_sk_RecvFrom, int s, void *b, unsigned long n, int f, void *a, unsigned *al) { if (dm_online()) return dmo_recvfrom(s, b, n, f, a, al); (void)s; (void)b; (void)n; (void)f; (void)a; (void)al; return sk_fail(); }
HLE_FORCE("_ZN2nn6socket4SendEiPKvmNS0_7MsgFlagE", "bsd socket service (cut)", long, h_sk_Send, int s, const void *b, unsigned long n, int f) { if (dm_online()) return dmo_sendto(s, b, n, f, 0, 0); (void)s; (void)b; (void)n; (void)f; return sk_fail(); }
HLE_FORCE("_ZN2nn6socket11GetAddrInfoEPKcS2_PKNS0_8AddrInfoEPPS3_", "name resolution (cut): EAI_FAIL", int, h_sk_GetAddrInfo, const char *h, const char *s, const void *hint, void **res) { if (dm_online()) return dmo_getaddrinfo_nn(h, s, hint, res);
    (void)h; (void)s; (void)hint; if (res) *res = NULL; return 4;   // EAI_FAIL
}
HLE_FORCE("_ZN2nn6socket12FreeAddrInfoEPNS0_8AddrInfoE", "pairs with GetAddrInfo (cut)", void, h_sk_FreeAddrInfo, void *ai) { if (dm_online()) { dmo_freeaddrinfo_nn(ai); return; } (void)ai; }
HLE_FORCE("_ZN2nn6socket16GetHostEntByNameEPKcPKNS0_14ResolverOptionEm", "name resolution (cut): NULL", void *, h_sk_GetHostEnt, const char *n, const void *o, unsigned long c) { if (dm_online()) return dmo_gethostbyname(n); (void)n; (void)o; (void)c; t_skerr = SK_ENETDOWN; return NULL; }
HLE_FORCE("_ZN2nn6socket13GetHostByNameEPKc", "name resolution (cut): NULL", void *, h_sk_GetHostByName, const char *n) { if (dm_online()) return dmo_gethostbyname(n); (void)n; t_skerr = SK_ENETDOWN; return NULL; }
HLE_FORCE("_ZN2nn6socket12GetLastErrorEv", "socket error code kept by the host", int, h_sk_GetLastError, void) { if (dm_online()) return dmo_errno(); return t_skerr; }
// C interface
SKF("nnsocketSocket", h_nsk_Socket, int a, int b, int c) { if (dm_online()) return dmo_socket(a, b, c); (void)a; (void)b; (void)c; return sk_fail(); }
SKF("nnsocketBind", h_nsk_Bind, int s, const void *a, unsigned n) { if (dm_online()) return dmo_bind(s, a, n); (void)s; (void)a; (void)n; return sk_fail(); }
SKF("nnsocketConnect", h_nsk_Connect, int s, const void *a, unsigned n) { if (dm_online()) return dmo_connect(s, a, n); (void)s; (void)a; (void)n; return sk_fail(); }
SKF("nnsocketClose", h_nsk_Close, int s) { if (dm_online()) return dmo_close(s); (void)s; return sk_fail(); }
SKF("nnsocketGetPeerName", h_nsk_GetPeerName, int s, void *a, unsigned *n) { if (dm_online()) return dmo_sockname(s, a, n, 1); (void)s; (void)a; (void)n; return sk_fail(); }
SKF("nnsocketGetSockName", h_nsk_GetSockName, int s, void *a, unsigned *n) { if (dm_online()) return dmo_sockname(s, a, n, 0); (void)s; (void)a; (void)n; return sk_fail(); }
SKF("nnsocketGetSockOpt", h_nsk_GetSockOpt, int s, int l, int o, void *v, unsigned *n) { if (dm_online()) return dmo_getsockopt(s, l, o, v, n); (void)s; (void)l; (void)o; (void)v; (void)n; return sk_fail(); }
SKF("nnsocketSetSockOpt", h_nsk_SetSockOpt, int s, int l, int o, const void *v, unsigned n) { if (dm_online()) return dmo_setsockopt(s, l, o, v, n); (void)s; (void)l; (void)o; (void)v; (void)n; return sk_fail(); }
SKF("nnsocketFcntl", h_nsk_Fcntl, int s, int c, int arg) { if (dm_online()) return dmo_fcntl(s, c, arg); (void)s; (void)c; (void)arg; (void)s; (void)c; return sk_fail(); }
SKF("nnsocketPoll", h_nsk_Poll, void *f, unsigned long n, int t) { return dm_sk_poll(f, n, t); }
SKF("nnsocketResolverGetOption", h_nsk_ResGet, void *o, int k) { if (dm_online()) return 0; (void)o; (void)k; return sk_fail(); }
SKF("nnsocketResolverSetOption", h_nsk_ResSet, const void *o) { if (dm_online()) return 0; (void)o; return sk_fail(); }
HLE_FORCE("nnsocketRecv", "bsd socket service (cut)", long, h_nsk_Recv, int s, void *b, unsigned long n, int f) { if (dm_online()) return dmo_recvfrom(s, b, n, f, 0, 0); (void)s; (void)b; (void)n; (void)f; return sk_fail(); }
HLE_FORCE("nnsocketSend", "bsd socket service (cut)", long, h_nsk_Send, int s, const void *b, unsigned long n, int f) { if (dm_online()) return dmo_sendto(s, b, n, f, 0, 0); (void)s; (void)b; (void)n; (void)f; return sk_fail(); }
HLE_FORCE("nnsocketGetHostByNameWithOptions", "name resolution (cut): NULL", void *, h_nsk_GetHost, const char *n, const void *o, unsigned long c) { if (dm_online()) return dmo_gethostbyname(n); (void)n; (void)o; (void)c; t_skerr = SK_ENETDOWN; return NULL; }
// A handle that lets a name lookup be cancelled (Mario Tennis Aces). Online lookups here finish before they return, so there is never anything to cancel.
// shortcut: Cancel does nothing; give lookups a cancel path if one ever has to stop while it waits
static _Atomic int g_cancel_ids;
SKF("nnsocketRequestCancelHandle", h_nsk_CancelHandle, void) { if (dm_online()) return atomic_fetch_add(&g_cancel_ids, 1) + 1; return sk_fail(); }
SKF("nnsocketCancel", h_nsk_Cancel, int id) { if (dm_online()) return 0; (void)id; return sk_fail(); }
HLE_FORCE("nnsocketGetLastErrno", "socket error code kept by the host", int, h_nsk_GetErrno, void) { if (dm_online()) return dmo_errno(); return t_skerr; }
HLE_FORCE("nnsocketSetLastErrno", "socket error code kept by the host", void, h_nsk_SetErrno, int e) { if (dm_online()) { dmo_set_errno(e); return; } t_skerr = e; }

// ---------- ssl: hle_online_ssl.c (offline the same cut answers; online, TLS to Nextendo). The C interface (nnssl*) is the SDK calling those methods ----------
