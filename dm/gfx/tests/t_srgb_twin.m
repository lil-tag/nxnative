// An sRGB and a linear texture over the same storage and description are one Metal texture and a view of it (alias_twin), as Mario Tennis Aces' composite and anti-aliasing pass need.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t *g_mod; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int checks, failed;
#define CHECK(c, label) do { checks++; if (!(c)) { failed++; fprintf(stderr, "FAIL: %s\n", label); } } while (0)
int main(void) { @autoreleasepool {
    mtl_init(); mtl_sub_begin();
    tex_t srgb = {.magic=MAGIC('T','E','X',0), .serial=99501};
    srgb.d = (nvn_texdesc){.target=NVN_TEXTURE_TARGET_2D, .format=NVN_FORMAT_RGBA8_SRGB, .width=4, .height=4, .depth=1, .levels=1, .samples=1};
    nvn_tex_layout(&srgb.d, &srgb.L); tex_t lin = srgb, other = srgb;
    lin.serial++; lin.d.format = NVN_FORMAT_RGBA8;
    other.serial += 2; other.d.format = NVN_FORMAT_RGBA8; other.d.width = 8; nvn_tex_layout(&other.d, &other.L);
    mtl_tex_init(&srgb); mtl_tex_init(&lin); mtl_tex_init(&other);
    id<MTLTexture> base = tex_mtl(&srgb), view = tex_mtl(&lin);
    CHECK(base.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB && !base.parentTexture, "the sRGB texture is a texture of its own");
    CHECK(view.parentTexture == base && view.pixelFormat == MTLPixelFormatRGBA8Unorm, "the linear one over the same storage is a view of it in its own format");
    CHECK(alias_has(view) && tex_mtl(&lin) == view, "the view is shared like any texture");
    CHECK(tex_mtl(&other).parentTexture == nil, "another description is not a twin");
    pthread_mutex_lock(&g_smu); CHECK(alias_viewed(base) && !alias_viewed(view), "the viewed texture is known as viewed"); pthread_mutex_unlock(&g_smu);
    id<MTLTexture> up = tex_upgrade(&lin, view, MTLTextureUsageRenderTarget, MTLPixelFormatRGBA8Unorm);
    id<MTLTexture> nbase = tex_mtl(&srgb);
    CHECK(nbase != base && (nbase.usage & MTLTextureUsageRenderTarget), "an upgrade through the view upgrades the texture it views");
    CHECK(up.parentTexture == nbase && tex_mtl(&lin) == up && up.pixelFormat == MTLPixelFormatRGBA8Unorm, "the view moves to a view of the upgraded texture");
    CHECK(alias_has(up) && !alias_has(view) && alias_has(nbase) && !alias_has(base), "sharing membership follows both");
    uint32_t pixels[16]; for (int i = 0; i < 16; i++) pixels[i] = 0x80402010u + i;
    id<MTLBuffer> src = [g_dev newBufferWithBytes:pixels length:sizeof pixels options:MTLResourceStorageModeShared]; res_add(src);
    id<MTLBuffer> dst = [g_dev newBufferWithLength:sizeof pixels options:MTLResourceStorageModeShared]; res_add(dst);
    id<MTL4ComputeCommandEncoder> encoder = blit();
    enc_b_copy_buffer_texture(encoder, src, 0, 16, sizeof pixels, MTLSizeMake(4,4,1), nbase, 0, 0, MTLOriginMake(0,0,0));
    encoder = blit(); enc_b_copy_texture_buffer(encoder, tex_mtl(&lin), 0, 0, MTLOriginMake(0,0,0), MTLSizeMake(4,4,1), dst, 0, 16, sizeof pixels);
    mtl_sub_commit(); mtl_drain();
    CHECK(!memcmp(dst.contents, pixels, sizeof pixels), "the linear view reads the bytes written through the sRGB texture");
    printf("sRGB / linear twins: %d checks, %d failed\n", checks, failed);
    return failed != 0;
} }
