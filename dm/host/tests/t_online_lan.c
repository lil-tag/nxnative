// LAN play (DM_LAN=1, hle_online.c): the real sockets without Nextendo. Names are refused so nothing leaves the local network, and a send to 255.255.255.255, which the Mac refuses, goes to the
// adapter's broadcast address. Sends only: a socket bound to every address would make the macOS firewall ask about this test each time it is rebuilt
#include "../hle_online.c"

void dm_thread_native_wait(int entering) { (void)entering; }
void dm_guest_write_prepare(void *addr, size_t len) { (void)addr; (void)len; }
module_t *module_of(const void *p) { (void)p; return NULL; }
static __thread int g_guest_errno;
int *dm_guest_errno(void) { return &g_guest_errno; }

static int checks, fails;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL: %s\n", msg); } } while (0)

int main(void) {
    setenv("DM_LAN", "1", 1); unsetenv("DM_NEXTENDO");
    CHECK(dm_lan() && dm_online(), "DM_LAN=1 turns the sockets on");

    void *res = NULL;
    CHECK(dmo_getaddrinfo("g23380901-lp1.s.n.srv.nintendo.net", "443", NULL, &res) != 0 && !res, "a Nintendo server name is neither looked up nor sent to Nextendo");
    CHECK(dmo_getaddrinfo("example.com", "443", NULL, &res) != 0 && !res, "no other name is looked up either");
    CHECK(!dmo_gethostbyname("example.com"), "gethostbyname refuses names too");
    CHECK(dmo_getaddrinfo("192.168.1.20", "30000", NULL, &res) == 0 && res, "a numeric address works");
    if (res) dmo_freeaddrinfo(res);

    if (!dmo_ip(NULL)) printf("t_online_lan: no network adapter, broadcast skipped\n");
    else {
        int u = dmo_socket(AF_INET, SOCK_DGRAM, 0), one = 1;
        struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(39317), .sin_addr.s_addr = INADDR_BROADCAST};
        uint8_t guest[16]; memcpy(guest, &to, 16); guest[0] = 0;   // the SDK's sockaddr_in has no length byte
        CHECK(dmo_setsockopt(u, SOL_SOCKET, SO_BROADCAST, &one, sizeof one) == 0, "SO_BROADCAST passes through");
        CHECK(dmo_sendto(u, "search", 6, 0, guest, 16) == 6, "a send to 255.255.255.255 goes out");
        dmo_close(u);
    }
    printf("t_online_lan: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
