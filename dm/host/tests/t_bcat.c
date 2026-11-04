// Delivery cache from a Nextendo package (hle_bcat.c): a zip made here with /usr/bin/zip is "downloaded" through curl from a file:// address in place of nextendo.network into a
// temporary HOME, then served through the real hooks, looked up by their mangled names. Expected values are hard-coded: raid/a.txt holds "hello" (MD5 5d41402a…), news/b.bin
// 4000 bytes of a repeating pattern (stored deflated by zip), "bad name/x" and raid/sub/c are left out (a space; a second level)
static char g_url[1100];
#define BCAT_URL g_url
#include "../hle_bcat.c"
#include <mach-o/getsect.h>
#include <unistd.h>

static int g_online = 1;
int dm_online(void) { return g_online; }
int dm_lan(void) { return 0; }
static int checks, fails;
extern const struct mach_header_64 _mh_execute_header;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL: %s\n", msg); } } while (0)
static void *hook(const char *name) {
    unsigned long bytes = 0;
    const hle_force_ent *e = (const hle_force_ent *)getsectiondata(&_mh_execute_header, "__DATA", "swforce", &bytes);
    void *fn = NULL; unsigned count = 0;
    for (size_t i = 0; e && i < bytes / sizeof *e; i++) if (!strcmp(e[i].name, name)) { count++; fn = (void *)e[i].fn; }
    CHECK(count == 1 && fn, name);
    return count == 1 ? fn : NULL;
}
static void put(const char *path, const void *d, size_t n) { FILE *f = fopen(path, "wb"); fwrite(d, 1, n, f); fclose(f); }
int main(void) {
    char tmp[] = "/tmp/t_bcat.XXXXXX", p[1200], id[17];
    if (!mkdtemp(tmp)) return 2;
    snprintf(id, sizeof id, "%s", DM_GAME_ID); for (int i = 0; id[i]; i++) id[i] = (char)tolower((unsigned char)id[i]);
    snprintf(p, sizeof p, "mkdir -p %s/src/raid/sub '%s/src/bad name' %s/src/news %s/home/Library/Application\\ Support", tmp, tmp, tmp, tmp); if (system(p)) return 2;
    snprintf(p, sizeof p, "%s/src/raid/a.txt", tmp); put(p, "hello", 5);
    snprintf(p, sizeof p, "%s/src/raid/sub/c", tmp); put(p, "c", 1);
    snprintf(p, sizeof p, "%s/src/bad name/x", tmp); put(p, "x", 1);
    uint8_t big[4000]; for (int i = 0; i < 4000; i++) big[i] = (uint8_t)(i % 7);
    snprintf(p, sizeof p, "%s/src/news/b.bin", tmp); put(p, big, sizeof big);
    snprintf(p, sizeof p, "cd %s/src && /usr/bin/zip -qr ../pkg.zip . && mv ../pkg.zip ../%s", tmp, id); if (system(p)) return 2;
    snprintf(g_url, sizeof g_url, "file://%s/", tmp);
    snprintf(p, sizeof p, "%s/home", tmp); setenv("HOME", p, 1);

    Result (*mount)(void) = hook("_ZN2nn4bcat25MountDeliveryCacheStorageEv");
    Result (*sync_)(uint8_t *) = hook("_ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressE");
    Result (*en)(int *, char (*)[32], int) = hook("_ZN2nn4bcat31EnumerateDeliveryCacheDirectoryEPiPNS0_13DirectoryNameEi");
    Result (*dopen)(char **, const char *) = hook("_ZN2nn4bcat22DeliveryCacheDirectory4OpenEPKc");
    Result (*dread)(char **, int *, uint8_t *, int) = hook("_ZN2nn4bcat22DeliveryCacheDirectory4ReadEPiPNS0_27DeliveryCacheDirectoryEntryEi");
    void (*dclose)(char **) = hook("_ZN2nn4bcat22DeliveryCacheDirectory5CloseEv");
    Result (*fopen_)(void **, const char *) = hook("_ZN2nn4bcat17DeliveryCacheFile4OpenEPKc");
    Result (*fread_)(void **, uint64_t *, int64_t, void *, uint64_t) = hook("_ZN2nn4bcat17DeliveryCacheFile4ReadEPmlPvm");
    int64_t (*fsize)(void **) = hook("_ZN2nn4bcat17DeliveryCacheFile7GetSizeEv");
    void (*fdtor)(void **) = hook("_ZN2nn4bcat17DeliveryCacheFileD1Ev");
    if (fails) { printf("t_bcat: hooks missing\n"); return 1; }

    g_online = 0; CHECK(mount() == RES_FAIL, "offline: mount fails (nothing delivered)"); g_online = 1;
    CHECK(mount() == RES_OK, "online: mount succeeds");
    snprintf(p, sizeof p, "%s/home/Library/Application Support/nxnative/bcat/%s.zip", tmp, DM_GAME_ID); struct stat st;
    CHECK(!stat(p, &st) && st.st_size > 0, "the package is kept as nxnative/bcat/<title ID>.zip");
    uint8_t progress[0x260] = {0}; CHECK(sync_(progress) == RES_OK && *(uint32_t *)(progress + 0x58) == 9 && *(uint32_t *)(progress + 0x5c) == 0, "sync: finished (status 9, result 0)");
    char names[8][32]; int n = -1;
    CHECK(en(&n, names, 8) == RES_OK && n == 2, "two directories (raid, news; 'bad name' left out)");
    int raid = 0, news = 0; for (int i = 0; i < n; i++) { raid += !strcmp(names[i], "raid"); news += !strcmp(names[i], "news"); }
    CHECK(raid == 1 && news == 1, "the directories are raid and news");
    char *dir = NULL; uint8_t ent[4][0x38]; int k = -1;
    CHECK(dopen(&dir, "nope") == RES_OK && dir && dread(&dir, &k, &ent[0][0], 4) == RES_OK && k == 0, "a missing directory opens empty (Scarlet's stadium)");
    dclose(&dir);
    CHECK(dopen(&dir, "raid") == RES_OK && dir, "open raid");
    CHECK(dread(&dir, &k, &ent[0][0], 4) == RES_OK && k == 1, "raid lists one file (raid/sub/c left out)");
    static const uint8_t md5_hello[16] = {0x5d,0x41,0x40,0x2a,0xbc,0x4b,0x2a,0x76,0xb9,0x71,0x9d,0x91,0x10,0x17,0xc5,0x92};
    CHECK(!strcmp((char *)ent[0], "a.txt") && *(int64_t *)(ent[0] + 0x20) == 5 && !memcmp(ent[0] + 0x28, md5_hello, 16), "entry a.txt, 5 bytes, MD5 of \"hello\"");
    dclose(&dir); CHECK(!dir, "close clears the directory");
    void *f = NULL; uint64_t done = 0; uint8_t buf[5000];
    CHECK(fopen_(&f, "raid/missing") == 0x27a, "a missing file: bcat NotFound");
    CHECK(fopen_(&f, "news/b.bin") == RES_OK && fsize(&f) == 4000, "news/b.bin opens, 4000 bytes (deflated in the zip)");
    CHECK(fread_(&f, &done, 3990, buf, sizeof buf) == RES_OK && done == 10 && buf[0] == 3990 % 7 && buf[9] == 3999 % 7, "read at offset 3990: the last 10 bytes");
    CHECK(fread_(&f, &done, 0, buf, sizeof buf) == RES_OK && done == 4000 && !memcmp(buf, big, 4000), "the whole file reads back");
    fdtor(&f); CHECK(!f, "the destructor closes the file");
    snprintf(p, sizeof p, "rm -rf %s", tmp); (void)!system(p);
    printf("t_bcat: %d checks, %d failed\n", checks, fails); return fails != 0;
}
