// Online play (hle_online.c): Nextendo's redirect rules and nsd templates, the SDK's addrinfo layout, and a real UDP socket on loopback through the guest's numbering
// (small descriptors, Linux errno values, O_NONBLOCK 0x800, POLLNVAL for an unknown descriptor). No network is needed: the lookups that go to Nextendo are numeric
#include "../hle_online.c"

void dm_thread_native_wait(int entering) { (void)entering; }
module_t *module_of(const void *p) { (void)p; return NULL; }   // the trace's caller names (DM_NEXTENDO_TRACE): no game modules here

static int checks, fails;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL: %s\n", msg); } } while (0)

static const char *ip_of(struct addrinfo *ai, char *buf) {
    return ai ? inet_ntop(AF_INET, &((struct sockaddr_in *)ai->ai_addr)->sin_addr, buf, INET_ADDRSTRLEN) : "none";
}

int main(void) {
    setenv("DM_NEXTENDO", "1", 1);
    unsetenv("DM_NEXTENDO_SERVER"); unsetenv("DM_NEXTENDO_NAT");
    CHECK(dm_online(), "DM_NEXTENDO=1 turns online play on");

    // the redirect rules, most specific first
    CHECK(dmo_redirect("g2b309e01-lp1.s.n.srv.nintendo.net") && !strcmp(dmo_redirect("g2b309e01-lp1.s.n.srv.nintendo.net"), "164.132.111.120"), "Mario Kart's game server goes to the NAT address");
    CHECK(!strcmp(dmo_redirect("nncs2-lp1.n.n.srv.nintendo.net"), "164.132.111.120"), "the second NAT check goes to the NAT address");
    CHECK(!strcmp(dmo_redirect("nncs1-lp1.n.n.srv.nintendo.net"), "51.178.29.194"), "the first NAT check goes to the server");
    CHECK(!strcmp(dmo_redirect("g23380901-lp1.s.n.srv.nintendo.net"), "51.178.29.194"), "Smash's game server goes to the server");
    CHECK(!strcmp(dmo_redirect("E0D67C509FB203858EBCB2FE3F88C2AA.baas.nintendo.com"), "51.178.29.194"), "names match in any case");
    CHECK(!strcmp(dmo_redirect("demonware.net"), "51.178.29.194") && !strcmp(dmo_redirect("stun.eu.demonware.net"), "51.178.29.194"), "Demonware, bare and wildcard");
    CHECK(!dmo_redirect("example.com") && !dmo_redirect("nintendo.net.example.com"), "other names resolve normally");
    setenv("DM_NEXTENDO_SERVER", "127.0.0.2", 1);
    CHECK(!strcmp(dmo_redirect("g23380901-lp1.s.n.srv.nintendo.net"), "127.0.0.2"), "DM_NEXTENDO_SERVER overrides the server");
    unsetenv("DM_NEXTENDO_SERVER");

    // nsd templates
    char fq[256];
    CHECK(dmo_nsd_resolve(fq, "g2b309e01-%.s.n.srv.nintendo.net") == RES_OK && !strcmp(fq, "g2b309e01-lp1.s.n.srv.nintendo.net"), "nsd puts lp1 where the % is");
    dmo_nsd_resolve(fq, "accounts.nintendo.com");
    CHECK(!strcmp(fq, "e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com"), "the account server is Nextendo's BAAS");

    // the SDK's addrinfo: canonname at +0x18, addr at +0x20, next at +0x28 (Smash's sdk, FreeAddrinfoContents)
    CHECK(offsetof(struct addrinfo, ai_canonname) == 0x18 && offsetof(struct addrinfo, ai_addr) == 0x20 && offsetof(struct addrinfo, ai_next) == 0x28, "the Mac's addrinfo is the SDK's");
    void *res = NULL; char ip[INET_ADDRSTRLEN];
    CHECK(dmo_getaddrinfo("g23380901-%.s.n.srv.nintendo.net", "443", NULL, &res) == 0 && !strcmp(ip_of(res, ip), "51.178.29.194"), "a lookup of a Nintendo name gives Nextendo's address");
    CHECK(res && ntohs(((struct sockaddr_in *)((struct addrinfo *)res)->ai_addr)->sin_port) == 443, "the service becomes the port");
    dmo_freeaddrinfo(res);
    struct hostent *he = dmo_gethostbyname("nncs2-lp1.n.n.srv.nintendo.net");
    CHECK(he && he->h_length == 4 && he->h_addr_list[0] && !strcmp(inet_ntop(AF_INET, he->h_addr_list[0], ip, sizeof ip), "164.132.111.120"), "gethostbyname gives the NAT address");

    // a UDP socket on loopback, in the guest's numbering
    int a = dmo_socket(AF_INET, SOCK_DGRAM | SW_O_NONBLOCK, 0), b = dmo_socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(a >= 3 && b > a && b < GFD_MAX, "guest descriptors are small and start at 3");
    CHECK(dmo_fcntl(a, F_GETFL, 0) == SW_O_NONBLOCK && dmo_fcntl(b, F_GETFL, 0) == 0, "SOCK_NONBLOCK reads back as O_NONBLOCK 0x800");
    uint8_t sa[16] = {0, AF_INET};   // sin_len left at 0, as games do
    sa[4] = 127; sa[7] = 1;
    CHECK(dmo_bind(a, sa, sizeof sa) == 0, "bind to loopback");
    uint8_t got[16] = {0}; unsigned gl = sizeof got;
    CHECK(dmo_sockname(a, got, &gl, 0) == 0 && gl == 16 && got[0] == 0 && got[1] == AF_INET && (got[2] | got[3]), "getsockname gives the port, length byte 0");
    char buf[16] = {0};
    CHECK(dmo_recvfrom(a, buf, sizeof buf, 0, NULL, NULL) == -1 && dmo_errno() == 11, "an empty nonblocking socket fails with Linux EAGAIN 11");
    CHECK(dmo_sendto(b, "nextendo", 8, 0, got, sizeof got) == 8, "sendto loopback");
    sw_pollfd pf[2] = {{a, POLLIN, 0}, {999, POLLIN, 0}};
    CHECK(dmo_poll(pf, 1, 1000) == 1 && pf[0].revents & POLLIN, "poll waits for the datagram");
    CHECK(dmo_poll(pf, 2, 1000) == 2 && pf[0].revents & POLLIN && pf[1].revents == POLLNVAL, "an unknown descriptor reports POLLNVAL at once, beside the others");
    uint64_t rset[16] = {0}; rset[a >> 6] |= 1ull << (a & 63);
    int64_t tv[2] = {1, 0};
    CHECK(dmo_select(a + 1, rset, NULL, NULL, tv) == 1 && rset[a >> 6] >> (a & 63) & 1, "select through poll");
    uint8_t from[16]; unsigned fl = sizeof from;
    CHECK(dmo_recvfrom(a, buf, sizeof buf, 0, from, &fl) == 8 && !memcmp(buf, "nextendo", 8) && fl == 16 && from[1] == AF_INET, "recvfrom gets the datagram and its sender");
    int64_t to[2] = {2, 500000}, back[2] = {0}; unsigned bl = sizeof back;
    CHECK(dmo_setsockopt(b, SOL_SOCKET, SO_RCVTIMEO, to, sizeof to) == 0 && dmo_getsockopt(b, SOL_SOCKET, SO_RCVTIMEO, back, &bl) == 0 && back[0] == 2 && back[1] == 500000, "timeouts in the guest's 64-bit timeval");
    CHECK(dmo_close(a) == 0 && dmo_close(b) == 0, "close");
    CHECK(dmo_close(a) == -1 && dmo_errno() == 9, "closing twice fails with EBADF 9");
    CHECK(dmo_socket(AF_INET, SOCK_STREAM, 0) == a, "a closed descriptor is used again");

    uint32_t mask = 0, me = dmo_ip(&mask);
    fprintf(stderr, "t_online: this Mac's address %s\n", inet_ntop(AF_INET, &me, ip, sizeof ip));

    printf("t_online: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
