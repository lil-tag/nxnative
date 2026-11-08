// Capture compiles that share a stage function take turns (nvn_mtl.m cap_enter / cap_leave, upstream issue #815): a second compile of the same function waits for the first to leave,
// one of another function does not wait, a turn nobody gives back is waited for at most 2 s (and the late compile marks nothing), a library with no label takes no turn
#include "../nvn/nvn_mtl.m"
uint64_t g_t0_ns;
int dm_guest_as_contains(uintptr_t address) { (void)address; return 0; }
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t *g_mod; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int checks, failed;
static void check(int ok, const char *what) { checks++; if (!ok) { failed++; fprintf(stderr, "FAIL: %s\n", what); } }

static MTL4RenderPipelineDescriptor *desc(id<MTLLibrary> lib, NSString *vertex) {
    MTL4LibraryFunctionDescriptor *v = [MTL4LibraryFunctionDescriptor new]; v.library = lib; v.name = vertex;
    MTL4RenderPipelineDescriptor *d = [MTL4RenderPipelineDescriptor new]; d.vertexFunctionDescriptor = v;
    return d;
}
static double ms_since(uint64_t t0) { return (bp_now() - t0) / 1e6; }

int main(void) { @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    NSError *e = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nvertex float4 a() { return 0; }\nvertex float4 b() { return 0; }\n" options:nil error:&e];
    if (!lib) { fprintf(stderr, "no library: %s\n", e.localizedDescription.UTF8String); return 1; }
    lib.label = @"v1_b.metallib";

    // the same function: the second waits until the first leaves
    NSArray<NSString *> *first = cap_enter(desc(lib, @"a"));
    check(first.count == 1, "the first compile takes its turn");
    __block double waited = -1; dispatch_semaphore_t done = dispatch_semaphore_create(0);
    uint64_t t0 = bp_now();
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ NSArray<NSString *> *mine = cap_enter(desc(lib, @"a")); waited = ms_since(t0); cap_leave(mine); dispatch_semaphore_signal(done); });
    usleep(300000); cap_leave(first);
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    check(waited >= 250 && waited < 1500, "a compile of the same function waits for the first to leave");

    // another function does not wait
    first = cap_enter(desc(lib, @"a"));
    t0 = bp_now(); NSArray<NSString *> *other = cap_enter(desc(lib, @"b"));
    check(ms_since(t0) < 100 && other.count == 1, "a compile of another function does not wait");
    cap_leave(other);

    // a turn nobody gives back: at most 2 s, and the late compile marks nothing it would then release under the holder
    t0 = bp_now(); NSArray<NSString *> *late = cap_enter(desc(lib, @"a"));
    double w = ms_since(t0);
    check(w >= 1900 && w < 3000 && late.count == 0, "a turn nobody gives back is waited for at most 2 s");
    cap_leave(late); cap_leave(first);
    check(g_cap_busy.count == 0, "every turn is given back");

    // no label, no key: no turn
    id<MTLLibrary> plain = [dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nvertex float4 a() { return 0; }\n" options:nil error:&e];
    check(cap_enter(desc(plain, @"a")).count == 0, "a library with no label takes no turn");

    printf("t_cap_turn: %d checks, %d failed\n", checks, failed);
    return failed != 0;
} }
