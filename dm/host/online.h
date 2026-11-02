// Online play through Nextendo Network (hle_online.c). The network stubs in hle_net.c, hle_offline.c and hle_misc.c call these when dm_online() is on.
#pragma once
#include <stdint.h>

int dm_online(void);   // DM_NEXTENDO=1
int dmo_trace(void);   // DM_NEXTENDO_TRACE=1: every socket and TLS call in the log
int dmo_errno(void);
void dmo_set_errno(int e);
int dmo_socket(int family, int type, int proto);
int dmo_close(int s);
int dmo_shutdown(int s, int how);
int dmo_bind(int s, const void *a, unsigned n);
int dmo_connect(int s, const void *a, unsigned n);
int dmo_listen(int s, int backlog);
int dmo_accept(int s, void *a, unsigned *n);
int dmo_sockname(int s, void *a, unsigned *n, int peer);
int dmo_getsockopt(int s, int level, int opt, void *v, unsigned *n);
int dmo_setsockopt(int s, int level, int opt, const void *v, unsigned n);
int dmo_fcntl(int s, int cmd, int arg);
long dmo_recvfrom(int s, void *b, unsigned long n, int f, void *a, unsigned *al);   // a NULL: recv
long dmo_sendto(int s, const void *b, unsigned long n, int f, const void *a, unsigned al);   // a NULL: send
long dmo_sendmsg(int s, const void *msg, int f);   // the SDK's msghdr (Linux layout)
long dmo_recvmsg(int s, void *msg, int f);
int dmo_eventfd(uint64_t v, int flags);   // an eventfd descriptor, polled like a socket (gRPC's wakeup)
int dmo_eventfd_read(int s, uint64_t *v);
int dmo_eventfd_write(int s, uint64_t v);
int *dm_guest_errno(void);   // thread.c: the SDK's errno for this thread, NULL where the SDK keeps it elsewhere
int dmo_poll(void *fds, uint64_t n, int ms);
int dmo_select(int nfds, uint64_t *r, uint64_t *w, uint64_t *e, const int64_t *tv);
const char *dmo_redirect(const char *host);   // Nextendo's address for one of Nintendo's server names, else NULL
int dmo_is_server(uint32_t ip);               // one of Nextendo's addresses (network byte order)
int dmo_is_trusted(uint32_t ip);              // a TLS peer: Nextendo's, or an address Nextendo handed out (not one an internet name resolved to)
int dmo_host_for(uint32_t ip, char *out, unsigned long cap);   // the name last looked up for that address
int dmo_host_fd(int s);                       // the Mac's descriptor behind a guest one, else -1
uint32_t dmo_nsd_resolve(char *out, const char *in);
int dmo_getaddrinfo(const char *node, const char *service, const void *hints, void **res);
void dmo_freeaddrinfo(void *ai);
int dmo_getaddrinfo_nn(const char *node, const char *service, const void *hints, void **res);   // nn::socket::AddrInfo: ai_addr before ai_canonname
void dmo_freeaddrinfo_nn(void *ai);
void *dmo_gethostbyname(const char *node);
uint32_t dmo_ip(uint32_t *mask);
uint32_t dmo_ip_config(uint32_t *ip, uint32_t *mask, uint32_t *gw, uint32_t *dns1, uint32_t *dns2);
uint32_t dmo_ssl_import(uint64_t *id);   // hle_online_ssl.c: a certificate the game imports (kept nowhere: Nextendo has its own)
uint32_t dmo_ssl_builtin_size(uint32_t *out, const int32_t *ids, unsigned n);   // the firmware's root certificates (BuiltInManager)
uint32_t dmo_ssl_builtin(void **out, uint8_t *buf, unsigned cap, const int32_t *ids, unsigned n);
int dm_sysdata_dir(char *dir, unsigned long n, uint64_t id);   // hle_fs.c: the player's firmware archive folder for a system data ID, 0 when there is none
// hle_online_account.c: the Nextendo sign-in (DM_NEXTENDO_PID / _TOKEN / _NAME / _COUNTRY, or ~/Library/Application Support/nxnative/nextendo.env)
int dmo_signed_in(void);   // online, with a pid and a token
uint64_t dmo_pid(void);
const char *dmo_name(void);
unsigned long dmo_id_token(char *out, unsigned long cap);   // the token's length; out gets as much as fits
uint32_t dmo_na_info(void *info);
