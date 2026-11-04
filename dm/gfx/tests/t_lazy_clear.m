// Clears that wait for a pass to take them must still land before anything can see their texture, and in order. Through the production replay; the pixels are read back from the GPU.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t *g_mod; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int checks, failed;
#define CHECK(c, label) do { checks++; if (!(c)) { failed++; printf("FAIL: %s\n", label); } } while (0)
enum { W = 4 };
static id<MTLCommandQueue> g_q;
static tex_t mk(uint64_t serial, id<MTLTexture> *out) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:W height:W mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; d.storageMode = MTLStorageModePrivate;
    *out = [g_dev newTextureWithDescriptor:d];
    tex_t t = {0}; t.magic = MAGIC('T','E','X',0); t.serial = serial; t.d = (nvn_texdesc){.target=NVN_TEXTURE_TARGET_2D, .format=NVN_FORMAT_RGBA8, .width=W, .height=W, .depth=1, .levels=1, .samples=1};
    res_add(*out); mtl_tex_init(&t); serial_put(serial, *out);
    return t;
}
static uint32_t pixel(id<MTLTexture> t) {
    id<MTLBuffer> b = [g_dev newBufferWithLength:W * W * 4 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> e = [cb blitCommandEncoder];
    [e copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(W, W, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:W * 4 destinationBytesPerImage:W * W * 4];
    [e endEncoding]; [cb commit]; [cb waitUntilCompleted];
    return ((const uint32_t *)b.contents)[0];
}
typedef struct { uint8_t bytes[4096]; size_t n; } list;
static void *add(list *l, uint32_t op, size_t size) { op_hdr h = {op, (uint32_t)(sizeof h + size)}; memcpy(l->bytes + l->n, &h, sizeof h); void *p = l->bytes + l->n + sizeof h; memset(p, 0, size); l->n += sizeof h + size; return p; }
static void clr(list *l, const tex_t *t, float r, float g, float b) { p_clear_tex *c = add(l, OP_CLEAR_TEX, sizeof *c); c->tex = t; c->mask = 15; c->r = (NVNcopyRegion){0, 0, 0, W, W, 1}; c->c.f[0] = r; c->c.f[1] = g; c->c.f[2] = b; c->c.f[3] = 1; }
static void run(list *l) { op_hdr e = {OP_END, sizeof e}; memcpy(l->bytes + l->n, &e, sizeof e); state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); mtl_exec(l->bytes); mtl_sub_commit(); mtl_drain(); }
int main(void) { @autoreleasepool {
    if (!MTLCreateSystemDefaultDevice()) { puts("t_lazy_clear: no Metal device, skipped"); return 0; }
    mtl_init(); g_q = [g_dev newCommandQueue];
    id<MTLTexture> ma, mb; tex_t a = mk(99801, &ma), b = mk(99802, &mb);
    const uint32_t red = 0xff0000ff, green = 0xff00ff00, blue = 0xffff0000;
    list l = {0};
    clr(&l, &a, 1, 0, 0); run(&l);
    CHECK(pixel(ma) == red, "a clear with nothing after it lands by the end of the list");
    memset(&l, 0, sizeof l); clr(&l, &a, 1, 0, 0); clr(&l, &a, 0, 1, 0); run(&l);
    CHECK(pixel(ma) == green, "the newer of two clears wins");
    memset(&l, 0, sizeof l); clr(&l, &a, 0, 0, 1); clr(&l, &b, 0, 1, 0); run(&l);
    CHECK(pixel(ma) == blue && pixel(mb) == green, "clears of two textures both land");
    memset(&l, 0, sizeof l); clr(&l, &a, 1, 0, 0);
    { p_copy_t2t *c = add(&l, OP_COPY_T2T, sizeof *c); c->src = &a; c->dst = &b; c->sr = c->dr = (NVNcopyRegion){0, 0, 0, W, W, 1}; }
    clr(&l, &a, 0, 0, 1); run(&l);
    CHECK(pixel(mb) == red, "a copy reads the clear that waited before it");
    CHECK(pixel(ma) == blue, "the clear after the copy lands too");
    memset(&l, 0, sizeof l); clr(&l, &a, 1, 0, 0);
    { p_clear_tex *c = add(&l, OP_CLEAR_TEX, sizeof *c); c->tex = &a; c->mask = 15; c->r = (NVNcopyRegion){0, 0, 0, W / 2, W, 1}; c->c.f[1] = 1; c->c.f[3] = 1; }
    run(&l);
    { id<MTLBuffer> rb = [g_dev newBufferWithLength:W * W * 4 options:MTLResourceStorageModeShared]; id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> e = [cb blitCommandEncoder];
      [e copyFromTexture:ma sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(W, W, 1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:W * 4 destinationBytesPerImage:W * W * 4]; [e endEncoding]; [cb commit]; [cb waitUntilCompleted];
      const uint32_t *px = rb.contents; CHECK(px[0] == green && px[W - 1] == red, "a part-level clear keeps its order after the whole-level one that waited"); }
    uint64_t taken, landed; mtl_lc_stats(&taken, &landed);
    printf("t_lazy_clear: %d checks, %d failed (clears landed in passes of their own %llu)\n", checks, failed, (unsigned long long)landed);
    return failed != 0;
} }
