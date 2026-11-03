// bcat: data delivered from the server (news, events, schedules), read by the game from its "delivery cache" as directories of files.
// Offline (Super Mario Odyssey): Initialize talks to the bcat service (an SDK abort without it), so it is a no-op; mounting the delivery cache fails, which the game treats as "nothing
// delivered" (main+0x6c9258: a failure skips the read unless the caller asked for an error dialog, which only logs here); syncing fails.
// Online (Nextendo): the delivery cache is the package Nextendo publishes for the title, GET https://nextendo.network/api/bcat/<title ID> (no sign-in; a zip of <directory>/<file>,
// 204 with no body when nothing is published), the one Ryujinx-Nextendo installs as its bcat seed: Pokémon Scarlet's raid event and distribution_internet, Super Smash Bros. Ultimate's
// news and popups. It is fetched once per run, at the first mount or sync, and kept as ~/Library/Application Support/nxnative/bcat/<title ID>.zip; a failed fetch keeps the earlier
// package. Files are read straight out of the zip in memory, never extracted, so a package's names cannot reach the disk; only <directory>/<file> names of the Switch's own
// characters and lengths are served.
// Pokémon Scarlet 4.0.0 syncs on going online (main+0x2adacb8) and shows 2122-0008 (0x107a, built in the SDK's RequestSyncDeliveryCache without a service) unless the sync succeeds, then
// 2122-0001 (bcat NotFound, 0x27a) when the files it reads are missing. The SDK's DeliveryCacheDirectory and DeliveryCacheFile are one pointer each (the service object, 0 when closed;
// their destructors close it when set), which holds the host's open directory or file here.
#include "hle.h"
#include "online.h"
#include <compression.h>
#include <CommonCrypto/CommonDigest.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#ifndef BCAT_URL
#define BCAT_URL "https://nextendo.network/api/bcat/"
#endif
#define BCAT_NOT_FOUND 0x27au   // bcat (module 122) description 1
#define BCAT_NAME 32            // DirectoryName / FileName: 31 characters and the terminator

typedef struct { char dir[BCAT_NAME], file[BCAT_NAME]; uint16_t method; uint32_t csize, usize; const uint8_t *data; int digested; uint8_t digest[16]; } bcat_ent;
static uint8_t *g_zip; static bcat_ent *g_ent; static int g_nent;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;   // digests are filled in lazily

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
// A delivery cache name: what the bcat service accepts (letters, digits, '_', '-', '.'), 1-31 characters, not "." or ".."
static int name_ok(const char *s, size_t n) {
    if (!n || n >= BCAT_NAME || (n <= 2 && s[0] == '.' && s[n - 1] == '.')) return 0;
    for (size_t i = 0; i < n; i++) if (!(isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '-' || s[i] == '.')) return 0;
    return 1;
}
// The zip's central directory → entries; 0 if it is not a zip. Directory entries and names that are not <directory>/<file> are left out; only stored and deflated files are kept
static int parse(const uint8_t *z, size_t n, bcat_ent **out, int *count, int quiet) {
    *out = NULL; *count = 0;
    if (n < 22) return 0;
    size_t e = n - 22;
    while (u32(z + e) != 0x06054b50) { if (!e || n - e > 22 + 0xffff) return 0; e--; }
    size_t total = u16(z + e + 10), at = u32(z + e + 16);
    bcat_ent *v = calloc(total ? total : 1, sizeof *v); int k = 0;
    if (!v) return 0;
    for (size_t i = 0; i < total; i++) {
        if (at + 46 > n || u32(z + at) != 0x02014b50) { free(v); return 0; }
        const uint8_t *c = z + at; size_t nl = u16(c + 28), next = at + 46 + nl + u16(c + 30) + u16(c + 32);
        const char *name = (const char *)c + 46, *slash = nl && at + 46 + nl <= n ? memchr(name, '/', nl) : NULL;
        size_t lo = u32(c + 42), csize = u32(c + 20);
        if (slash && name_ok(name, (size_t)(slash - name)) && name_ok(slash + 1, nl - (size_t)(slash - name) - 1) && (u16(c + 10) == 0 || u16(c + 10) == 8)
            && lo + 30 <= n && u32(z + lo) == 0x04034b50) {
            size_t d = lo + 30 + u16(z + lo + 26) + u16(z + lo + 28);
            if (d <= n && csize <= n - d) {
                bcat_ent *t = &v[k++];
                memcpy(t->dir, name, (size_t)(slash - name)); memcpy(t->file, slash + 1, nl - (size_t)(slash - name) - 1);
                t->method = u16(c + 10); t->csize = (uint32_t)csize; t->usize = u32(c + 24); t->data = z + d;
            }
        } else if (!quiet && nl && name[nl - 1] != '/') LOG("[online] bcat: package entry %.*s left out (not <directory>/<file> with delivery cache names)", (int)nl, name);
        at = next;
    }
    *out = v; *count = k; return 1;
}
// A file's contents (malloc'd, usize bytes), or NULL
static uint8_t *contents(const bcat_ent *t) {
    uint8_t *d = malloc(t->usize ? t->usize : 1);
    if (!d) return NULL;
    if (t->method == 0 ? t->csize == t->usize && (memcpy(d, t->data, t->usize), 1)
                       : compression_decode_buffer(d, t->usize, t->data, t->csize, NULL, COMPRESSION_ZLIB) == t->usize) return d;   // COMPRESSION_ZLIB is raw deflate, zip's method 8
    free(d); return NULL;
}
static int zip_path(char *p, size_t n, const char *suffix) {
    const char *home = getenv("HOME");
    return home && snprintf(p, n, "%s/Library/Application Support/nxnative/bcat/%s%s", home, DM_GAME_ID, suffix) < (int)n;
}
static uint8_t *read_all(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); uint8_t *d = NULL; struct stat st;
    if (f && !fstat(fileno(f), &st) && st.st_size > 0 && st.st_size <= 64 << 20 && (d = malloc((size_t)st.st_size)) && fread(d, 1, (size_t)st.st_size, f) != (size_t)st.st_size) { free(d); d = NULL; }
    if (f) fclose(f);
    *n = d ? (size_t)st.st_size : 0; return d;
}
// Fetches the title's package into <title ID>.zip with curl (posix_spawn, no shell): an empty answer (204, nothing published) removes the kept package, anything that is not a zip is dropped
static void fetch(void) {
    char path[1024], tmp[1024], url[128], dir[1024];
    if (!zip_path(path, sizeof path, ".zip") || !zip_path(tmp, sizeof tmp, ".zip.part")) return;
    snprintf(dir, sizeof dir, "%s", path); *strrchr(dir, '/') = 0;
    *strrchr(dir, '/') = 0; mkdir(dir, 0755); dir[strlen(dir)] = '/'; mkdir(dir, 0755);   // nxnative/, then nxnative/bcat/
    int n = snprintf(url, sizeof url, "%s%s", BCAT_URL, DM_GAME_ID);
    for (int i = n - 16; i < n && i >= 0; i++) url[i] = (char)tolower((unsigned char)url[i]);
    char *argv[] = {"curl", "-sf", "--max-time", "10", "-o", tmp, url, NULL};
    pid_t pid; int st = -1;
    if (posix_spawn(&pid, "/usr/bin/curl", NULL, NULL, argv, NULL) || waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st)) {
        unlink(tmp); LOG("[online] bcat: Nextendo package download failed (curl status %d), the earlier one is kept", WIFEXITED(st) ? WEXITSTATUS(st) : -1); return;
    }
    size_t len; uint8_t *z = read_all(tmp, &len); bcat_ent *v; int k;
    if (!z) { unlink(tmp); unlink(path); LOG("[online] bcat: Nextendo publishes no package for this game"); return; }
    if (!parse(z, len, &v, &k, 1)) { unlink(tmp); LOG("[online] bcat: the Nextendo download is not a zip (%zu bytes), the earlier package is kept", len); }
    else if (rename(tmp, path)) { unlink(tmp); LOG("[online] bcat: cannot keep the Nextendo package (errno %d)", errno); }
    else free(v);
    free(z);
}
static void load(void) {
    char path[1024]; size_t len;
    if (dm_online()) fetch();
    if (!zip_path(path, sizeof path, ".zip") || !(g_zip = read_all(path, &len))) return;
    if (!parse(g_zip, len, &g_ent, &g_nent, 0)) { free(g_zip); g_zip = NULL; LOG("[online] bcat: %s is not a zip, nothing delivered", path); return; }
    LOG("[online] bcat: delivery cache from the Nextendo package, %d files", g_nent);
}
static void ensure(void) { pthread_once(&g_once, load); }
static int has_dir(const char *dir) { for (int i = 0; i < g_nent; i++) if (!strcmp(g_ent[i].dir, dir)) return 1; return 0; }
static bcat_ent *find(const char *dir, const char *file) { for (int i = 0; i < g_nent; i++) if (!strcmp(g_ent[i].dir, dir) && !strcmp(g_ent[i].file, file)) return &g_ent[i]; return NULL; }

HLE_FORCE("_ZN2nn4bcat10InitializeEv", "data delivery init (connects to the bcat service); the host has none, no-op", void, h_bcat_Init, void) {}
HLE_FORCE("_ZN2nn4bcat25MountDeliveryCacheStorageEv", "mount delivery cache: offline fails (nothing delivered); online the Nextendo package, fetched at the first mount or sync", Result, h_bcat_Mount, void) {
    if (!dm_online()) return RES_FAIL;
    ensure(); return RES_OK;
}
HLE_FORCE("_ZN2nn4bcat27UnmountDeliveryCacheStorageEv", "unmount delivery cache", void, h_bcat_Unmount, void) {}
// The SDK's DeliveryCacheProgress keeps the status at +0x58 and the result at +0x5c, and its Update does nothing while its service object (+0x20) is null, as the constructor leaves it,
// so the finished state written here stays: GetStatus answers 9 (done, Pokémon Scarlet main+0x2adb620), GetResult success
static Result bcat_sync(uint8_t *progress) {
    if (!dm_online()) return RES_FAIL;
    ensure();
    *(uint32_t *)(progress + 0x58) = 9; *(uint32_t *)(progress + 0x5c) = RES_OK;
    return RES_OK;
}
HLE_FORCE("_ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressE", "sync delivery cache: offline fails; online the Nextendo package, finished at once", Result, h_bcat_Sync, uint8_t *progress) { return bcat_sync(progress); }
HLE_FORCE("_ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressEPKc", "sync one delivery cache directory: as the whole sync", Result, h_bcat_SyncDir, uint8_t *progress, const char *dir) { (void)dir; return bcat_sync(progress); }
HLE_FORCE("_ZN2nn4bcat31EnumerateDeliveryCacheDirectoryEPiPNS0_13DirectoryNameEi", "list delivery cache directories: the package's", Result, h_bcat_Enum, int *count, char (*names)[BCAT_NAME], int max) {
    ensure(); int k = 0;
    for (int i = 0; i < g_nent && k < max; i++) {
        int seen = 0;
        for (int j = 0; j < i && !seen; j++) seen = !strcmp(g_ent[j].dir, g_ent[i].dir);
        if (!seen) { memset(names[k], 0, BCAT_NAME); strcpy(names[k++], g_ent[i].dir); }
    }
    *count = k; return RES_OK;
}

// ---------- directories: the open object holds a copy of the directory name ----------
static Result dir_open(char **self, const char *name) {
    ensure();
    if (*self) return RES_FAIL;
    // a directory the package lacks opens empty, as in Ryujinx-Nextendo's seed: Scarlet opens "stadium" (not in Nextendo's package) and shows 2122-0001 on NotFound
    if (!has_dir(name)) LOG("[online] bcat: directory %.31s not delivered, listed empty", name);
    *self = strdup(name); return *self ? RES_OK : RES_FAIL;
}
HLE_FORCE("_ZN2nn4bcat22DeliveryCacheDirectory4OpenEPKc", "open delivery cache directory", Result, h_bcat_DirOpen, char **self, const char *name) { return dir_open(self, name); }
HLE_FORCE("_ZN2nn4bcat22DeliveryCacheDirectory4OpenERKNS0_13DirectoryNameE", "open delivery cache directory", Result, h_bcat_DirOpenN, char **self, const char *name) { return dir_open(self, name); }
// DeliveryCacheDirectoryEntry: FileName (32), size (int64 at +0x20), digest (16 at +0x28): the MD5 of the contents, as the Switch's delivery cache lists it
HLE_FORCE("_ZN2nn4bcat22DeliveryCacheDirectory4ReadEPiPNS0_27DeliveryCacheDirectoryEntryEi", "list a delivery cache directory", Result, h_bcat_DirRead, char **self, int *count, uint8_t *out, int max) {
    if (!*self) return RES_FAIL;
    int k = 0;
    for (int i = 0; i < g_nent && k < max; i++) {
        bcat_ent *t = &g_ent[i];
        if (strcmp(t->dir, *self)) continue;
        uint8_t *e = out + 0x38 * k++;
        memset(e, 0, 0x38); strcpy((char *)e, t->file); *(int64_t *)(e + 0x20) = t->usize;
        pthread_mutex_lock(&g_lock);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"   // MD5 is the delivery cache's own digest format, not a security check
        if (!t->digested) { uint8_t *d = contents(t); if (d) { CC_MD5(d, t->usize, t->digest); t->digested = 1; free(d); } }
#pragma clang diagnostic pop
        memcpy(e + 0x28, t->digest, 16);
        pthread_mutex_unlock(&g_lock);
    }
    *count = k; return RES_OK;
}
HLE_FORCE("_ZN2nn4bcat22DeliveryCacheDirectory8GetCountEv", "delivery cache directory file count", int, h_bcat_DirCount, char **self) {
    int k = 0;
    if (*self) for (int i = 0; i < g_nent; i++) k += !strcmp(g_ent[i].dir, *self);
    return k;
}
HLE_FORCE("_ZN2nn4bcat22DeliveryCacheDirectory5CloseEv", "close delivery cache directory", void, h_bcat_DirClose, char **self) { free(*self); *self = NULL; }
HLE_FORCE("_ZN2nn4bcat22DeliveryCacheDirectoryD1Ev", "delivery cache directory destructor: closes it", void, h_bcat_DirDtor, char **self) { free(*self); *self = NULL; }

// ---------- files: the open object holds the decompressed contents ----------
typedef struct { size_t size; uint8_t data[]; } bcat_file;
static Result file_open(bcat_file **self, const char *dir, const char *file) {
    ensure();
    if (*self) return RES_FAIL;
    bcat_ent *t = find(dir, file); uint8_t *d;
    if (!t) { LOG("[online] bcat: file %.31s/%.31s not delivered", dir, file); return BCAT_NOT_FOUND; }
    if (!(d = contents(t)) || !(*self = malloc(sizeof **self + t->usize))) { free(d); LOG("[RED] bcat: file %s/%s cannot be read from the package", dir, file); return RES_FAIL; }
    (*self)->size = t->usize; memcpy((*self)->data, d, t->usize); free(d);
    return RES_OK;
}
HLE_FORCE("_ZN2nn4bcat17DeliveryCacheFile4OpenEPKc", "open delivery cache file <directory>/<file>", Result, h_bcat_FileOpen, bcat_file **self, const char *path) {
    char dir[BCAT_NAME] = {0}; const char *slash = strchr(path, '/');
    if (!slash || slash - path >= BCAT_NAME) return BCAT_NOT_FOUND;
    memcpy(dir, path, (size_t)(slash - path));
    return file_open(self, dir, slash + 1);
}
HLE_FORCE("_ZN2nn4bcat17DeliveryCacheFile4OpenERKNS0_13DirectoryNameERKNS0_8FileNameE", "open delivery cache file", Result, h_bcat_FileOpenN, bcat_file **self, const char *dir, const char *file) { return file_open(self, dir, file); }
HLE_FORCE("_ZN2nn4bcat17DeliveryCacheFile4ReadEPmlPvm", "read delivery cache file", Result, h_bcat_FileRead, bcat_file **self, uint64_t *done, int64_t off, void *buf, uint64_t size) {
    if (!*self || off < 0) return RES_FAIL;
    uint64_t n = (uint64_t)off >= (*self)->size ? 0 : (*self)->size - (uint64_t)off;
    if (n > size) n = size;
    memcpy(buf, (*self)->data + off, n); *done = n; return RES_OK;
}
HLE_FORCE("_ZN2nn4bcat17DeliveryCacheFile7GetSizeEv", "delivery cache file size", int64_t, h_bcat_FileSize, bcat_file **self) { return *self ? (int64_t)(*self)->size : 0; }
HLE_FORCE("_ZN2nn4bcat17DeliveryCacheFile5CloseEv", "close delivery cache file", void, h_bcat_FileClose, bcat_file **self) { free(*self); *self = NULL; }
HLE_FORCE("_ZN2nn4bcat17DeliveryCacheFileD1Ev", "delivery cache file destructor: closes it", void, h_bcat_FileDtor, bcat_file **self) { free(*self); *self = NULL; }
