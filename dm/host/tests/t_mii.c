// The player's Mii database (hle_mii.c): a Ryujinx NFDB file read into CharInfo, listed before the six default Miis, found again by its create ID, and an empty database without a file.
// The StoreData sample is one real Ryujinx Mii ("TAG"), its CharInfo values checked by hand against the bitfields in Mush8 (services/mii.rs). With the player's own MiiDatabase.dat
// present (Ryujinx's system save), every Mii in it must also convert within CharInfoRaw::Verify's ranges for the fields checked here
#include "../hle_mii.c"
void dm_applet_enter(const char *name) { (void)name; }
void dm_applet_leave(void) {}
uint64_t dm_entropy64(void) { return 0x0123456789abcdefull; }

static int checks, fails;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL: %s\n", msg); } } while (0)

static const char TAG[] = "2a3a34080a081708112b008d00a1694c6a4b62140d0b082b648244445400410047000000000000000000000000000000"
                          "643818dee5ec459a859cb8092c57879ab08150f8";

int main(void) {
    uint8_t store[MII_STORE], db[MII_NFDB_SIZE] = "NFDB";
    for (int i = 0; i < MII_STORE; i++) sscanf(TAG + 2 * i, "%2hhx", &store[i]);
    memcpy(db + 4, store, MII_STORE); db[MII_NFDB_SIZE - 4] = 1; db[MII_NFDB_SIZE - 3] = 1;

    CHECK(dm_mii_parse(db, 100) == 0 && dm_mii_parse((const uint8_t *)"NFDX", MII_NFDB_SIZE) == 0, "a short or foreign file is no database");
    CHECK(dm_mii_parse(db, sizeof db) == 1, "one Mii in the database");
    const mii_charinfo *c = &g_mii_db[0];
    CHECK(!memcmp(c->create_id, store + 0x30, 16) && c->nickname[0] == 'T' && c->nickname[1] == 'A' && c->nickname[2] == 'G' && !c->nickname[3], "create ID and nickname");
    CHECK(c->gender == 0 && c->height == 58 && c->build == 52, "gender, height, build");
    CHECK(c->hair_type == 42 && c->hair_color == 8 && !c->hair_flip, "hair");
    CHECK(c->eye_type == 43 && c->eye_color == 10 && c->eye_y == 13, "eyes");
    CHECK(c->eyebrow_type == 0 && c->eyebrow_color == 8 && c->eyebrow_y == 11, "eyebrows, Y stored minus 3");
    CHECK(c->mouth_color == 23 && c->beard_color == 8 && c->beard_type == 5, "mouth and beard");
    CHECK(c->glass_type == 13 && c->glass_color == 17 && c->mole_y == 20, "glasses and mole");

    mii_db(); dm_mii_parse(db, sizeof db);   // the hooks' one load first (this Mac's database, if any), then back to the sample
    uint8_t buf[8 * (sizeof(mii_charinfo) + 4)]; int n = -1;
    CHECK(h_mii_Count(NULL, MII_SRC_DATABASE) == 1 && h_mii_Count(NULL, MII_SRC_DEFAULT) == 6 && h_mii_Count(NULL, 3) == 7, "counts by source");
    CHECK(h_mii_GetE(NULL, &n, buf, 8, 3) == RES_OK && n == 7, "both sources listed");
    uint32_t src0, src1; memcpy(&src0, buf + sizeof(mii_charinfo), 4); memcpy(&src1, buf + (sizeof(mii_charinfo) + 4) + sizeof(mii_charinfo), 4);
    CHECK(!memcmp(buf, c, sizeof *c) && src0 == 0 && src1 == 1, "the player's Mii first (source 0), then the defaults (source 1)");
    CHECK(h_mii_Get(NULL, &n, buf, 2, MII_SRC_DEFAULT) == RES_OK && n == 2 && ((mii_charinfo *)buf)->nickname[0] == 'n', "defaults alone, as before");
    int idx = 9;
    CHECK(h_mii_Index(NULL, &idx, c) == RES_OK && idx == 0, "GetIndex finds the Mii by create ID");
    mii_charinfo other; mii_default(&other, 0, 0);
    CHECK(h_mii_Index(NULL, &idx, &other) == MII_RES_NOT_FOUND && idx == -1, "a default Mii is not in the database");
    mii_charinfo copy = *c; copy.height = 1;
    CHECK(h_mii_Latest(NULL, &copy, MII_SRC_DATABASE) == RES_OK && copy.height == 58, "UpdateLatest gives the database's version");
    CHECK(h_mii_Latest(NULL, &copy, MII_SRC_DATABASE) == MII_RES_NOT_UPDATED, "an unchanged Mii is not updated");
    CHECK(!h_mii_Full(NULL), "one Mii is not a full database");

    const char *home = getenv("HOME"); char p[1024]; FILE *f;
    snprintf(p, sizeof p, "%s/Library/Application Support/Ryujinx/bis/system/save/8000000000000030/0/MiiDatabase.dat", home ? home : "");
    if ((f = fopen(p, "rb"))) {
        static uint8_t real[MII_NFDB_SIZE]; size_t len = fread(real, 1, sizeof real, f); fclose(f);
        int k = dm_mii_parse(real, len), bad = 0;
        for (int i = 0; i < k; i++) { const mii_charinfo *m = &g_mii_db[i]; bad += m->gender > 1 || m->height > 127 || m->build > 127 || m->hair_type > 131 || m->eye_type > 59 || m->eyebrow_y < 3 || m->eyebrow_y > 18 || !m->nickname[0]; }
        printf("t_mii: this Mac's Ryujinx database: %d Mii(s)\n", k);
        CHECK(k > 0 && !bad, "every Mii of this Mac's Ryujinx database converts within range");
    }
    printf("t_mii: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
