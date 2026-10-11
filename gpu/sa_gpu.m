/*
 * sa_gpu.m — sa.c の焼きなましを Mac の GPU（Metal）で回すホスト。結果は sa.c とビット単位で同じ。
 *
 * 入出力は sa.c と同じ（.adj・.sched・-R・-S・-t・--init・--final・-q、標準出力の形）。
 * --order rand・--rng sm64 は無い（sa_fast と同じ）。-j は受け付けるが使わない。
 *
 *   -e a|b       エンジン（sa_gpu.metal の頭の説明）。既定は b
 *   --tg 数      1 threadgroup のスレッド数（a は 32 の倍数で threadgroup メモリに収まる数）。既定 a: 64、b: 32
 *                （小さいほうがレプリカが少なくても多くのコアに散らばる。大きい R では 32〜256 で差は 1 割以内）
 *   --chunk 数   1 回の dispatch で回す周回数の上限（既定: スレッドあたり約 2^20 頂点ぶん）
 *   --pf 0|1     エンジン a の先読み（隣の番号を 2 頂点先、語を 1 頂点先に読む）。既定は R ≤ 2048 のとき 1
 *                （レプリカが少ないと 1 本の SIMD グループの待ちが隠せないので効く。多いと命令が増えるぶん遅い）
 *   --amem tg|dev エンジン a の語の置き場。tg: threadgroup メモリ（1 SIMD グループ 4n バイト）、dev: device メモリのまま
 *                （キャッシュ任せ）。既定は threadgroup メモリ（32 KB）に SIMD グループが 4 本以上載る（n ≤ 2048）なら tg。
 *                n が大きいと tg では 1 コアに載る SIMD グループが少なすぎ、待ちが隠せない
 *   --gl 数      エンジン b の差 g の持ち方。2..16（既定 4）: 下の GL 桁だけビットスライスで持ち、上の桁は
 *                レプリカごとに device メモリ（sa_fast の bsasg と同じ）。0: 全部の桁をビットスライスで持つ
 *
 * 種と初期の塗り分けは CPU で sa.c と同じ規則で作り、GPU の形に並べ替えて渡す。
 * 時間（標準エラー）は、最初の dispatch を送ってから最後の dispatch が終わるまで（最初と最後のカット値を
 * 数えるカーネルは含めない）。GPU が実際に動いた時間の合計も出す。
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sa_gpu_metal.h" /* sa_gpu.metal を文字列にしたもの（Makefile が作る） */

#define MAX_TARGETS 8
#define NONE UINT64_MAX
#define GOLDEN 0x9E3779B97F4A7C15ull

static uint64_t sm64(uint64_t *s)
{
    uint64_t z = (*s += GOLDEN);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* ---- 入力（sa.c と同じ） ---- */

static int n, d;
static uint32_t *adj;
static int S;
static uint32_t *sweeps;
static uint32_t *th;
static uint64_t total_sweeps;
static int ntargets;
static int32_t targets[MAX_TARGETS];
static uint64_t master_seed = 1;

static FILE *xopen(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (!f) {
        perror(path);
        exit(1);
    }
    return f;
}

static long long read_int(FILE *f, const char *path)
{
    int c;
    long long v;
    for (;;) {
        c = fgetc(f);
        if (c == '#') {
            while ((c = fgetc(f)) != '\n' && c != EOF)
                ;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        } else {
            break;
        }
    }
    if (c == EOF) {
        fprintf(stderr, "%s: 途中で終わっている\n", path);
        exit(1);
    }
    ungetc(c, f);
    if (fscanf(f, "%lld", &v) != 1) {
        fprintf(stderr, "%s: 数でないものがある\n", path);
        exit(1);
    }
    return v;
}

static void load_graph(const char *path)
{
    FILE *f = xopen(path, "r");
    n = (int)read_int(f, path);
    d = (int)read_int(f, path);
    adj = malloc(sizeof(uint32_t) * n * d);
    for (int i = 0; i < n * d; i++) {
        long long u = read_int(f, path);
        if (u < 0 || u >= n || u == i / d) {
            fprintf(stderr, "%s: 頂点 %d の隣 %lld が範囲外か自己ループ\n", path, i / d, u);
            exit(1);
        }
        adj[i] = (uint32_t)u;
    }
    fclose(f);
    for (int v = 0; v < n; v++)
        for (int i = 0; i < d; i++) {
            uint32_t u = adj[v * d + i];
            int found = 0;
            for (int j = 0; j < d; j++)
                found += adj[u * d + j] == (uint32_t)v;
            if (found != 1) {
                fprintf(stderr, "%s: 辺 %d-%u が対称でないか多重辺\n", path, v, u);
                exit(1);
            }
        }
}

static void load_sched(const char *path)
{
    FILE *f = xopen(path, "r");
    int sd = (int)read_int(f, path);
    if (sd != d) {
        fprintf(stderr, "%s: d=%d だがグラフは d=%d\n", path, sd, d);
        exit(1);
    }
    S = (int)read_int(f, path);
    sweeps = malloc(sizeof(uint32_t) * S);
    th = malloc(sizeof(uint32_t) * S * (d + 1));
    total_sweeps = 0;
    for (int k = 0; k < S; k++) {
        sweeps[k] = (uint32_t)read_int(f, path);
        total_sweeps += sweeps[k];
        for (int j = 0; j <= d; j++)
            th[k * (d + 1) + j] = (uint32_t)read_int(f, path);
    }
    fclose(f);
}

/* ---- レプリカの初期化（sa.c の init_replica と同じ列） ---- */

static int R, RP;     /* レプリカ数と、組の大きさに切り上げた数（余りは捨てるレプリカ） */
static int NW;        /* ⌈n/64⌉ */
static uint64_t *seeds; /* RP */
static uint64_t *w0;    /* RP*NW: 初期の塗り分け（頂点 v の色 = 語 v/64 のビット v%64） */

static void init_replicas(void)
{
    NW = (n + 63) / 64;
    seeds = malloc(sizeof(uint64_t) * RP);
    w0 = malloc(sizeof(uint64_t) * (size_t)RP * NW);
    for (int p = 0; p < RP; p++) {
        uint64_t st = master_seed + ((uint64_t)p << 32) * GOLDEN;
        uint64_t seed;
        do
            seed = sm64(&st);
        while (seed == 0);
        seeds[p] = seed;
        for (int k = 0; k < NW; k++)
            w0[(size_t)p * NW + k] = sm64(&st);
    }
}

static inline int color0(int p, int v)
{
    return (w0[(size_t)p * NW + v / 64] >> (v % 64)) & 1;
}

/* 32×32 のビット行列の転置。a[i] のビット j ↔ a[j] のビット i（ブロックを入れ替えていく） */
static void transpose32(uint32_t a[32])
{
    uint32_t m = 0x0000FFFFu;
    for (int j = 16; j != 0; j >>= 1, m ^= m << j)
        for (int k = 0; k < 32; k = (k + j + 1) & ~j) {
            uint32_t t = ((a[k] >> j) ^ a[k + j]) & m;
            a[k] ^= t << j;
            a[k + j] ^= t;
        }
}

/* 32 レプリカ p0..p0+31 の、頂点 v0..v0+31（v0 は 32 の倍数）の色を頂点ごとの語にする:
 * out[j] のビット i = レプリカ p0+i の頂点 v0+j の色 */
static void pack32(int p0, int v0, uint32_t out[32])
{
    for (int i = 0; i < 32; i++)
        out[i] = (uint32_t)(w0[(size_t)(p0 + i) * NW + v0 / 64] >> (v0 % 64));
    transpose32(out);
}

/* ---- 出力 ---- */

static void dump_colors(const char *path, int (*get)(int p, int v))
{
    FILE *f = xopen(path, "w");
    char *line = malloc(n + 1);
    for (int p = 0; p < R; p++) {
        for (int v = 0; v < n; v++)
            line[v] = '0' + get(p, v);
        line[n] = '\n';
        fwrite(line, 1, n + 1, f);
    }
    free(line);
    fclose(f);
}

static void print_t(uint64_t t)
{
    if (t == NONE)
        printf(" -1");
    else
        printf(" %" PRIu64, t);
}

static void usage(void)
{
    fprintf(stderr,
            "使い方: sa_gpu -g graph.adj -s sched.sched [-R レプリカ数] [-S 主種] [-t 目標]...\n"
            "          [-e a|b] [--tg スレッド数] [--chunk 周回数] [--pf 0|1] [--amem tg|dev] [--gl 桁数]\n"
            "          [--init 初期の塗り分けの書き出し先] [--final 最後の塗り分けの書き出し先] [-q]\n");
    exit(2);
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

typedef struct {
    uint32_t seg0, seg1, tb_lo, tb_hi, nth, pad;
} params_t;

/* 最後の塗り分けを取り出すための GPU のバッファ（エンジンで並びが違う） */
static uint32_t *g_col;
static int g_eng, g_nth;
static int color_final(int p, int v)
{
    if (g_eng == 'a')
        return (g_col[(size_t)(p / 32) * n + v] >> (p % 32)) & 1;
    return (g_col[(size_t)v * g_nth + p / 32] >> (p % 32)) & 1;
}

static id<MTLBuffer> buf(id<MTLDevice> dev, size_t bytes)
{
    id<MTLBuffer> b = [dev newBufferWithLength:(bytes ? bytes : 16) options:MTLResourceStorageModeShared];
    if (!b) {
        fprintf(stderr, "バッファ %zu バイトが取れない\n", bytes);
        exit(1);
    }
    memset(b.contents, 0, b.length);
    return b;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        const char *gpath = NULL, *spath = NULL, *init_path = NULL, *final_path = NULL;
        int quiet = 0, eng = 'b', tg = 0, GL = 4, pf = -1, adev = -1;
        long chunk = 0;
        R = 16;
        for (int i = 1; i < argc; i++) {
            const char *a = argv[i];
            const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define NEED()       \
    do {             \
        if (!v)      \
            usage(); \
        i++;         \
    } while (0)
            if (!strcmp(a, "-g")) {
                NEED();
                gpath = v;
            } else if (!strcmp(a, "-s")) {
                NEED();
                spath = v;
            } else if (!strcmp(a, "-R")) {
                NEED();
                R = atoi(v);
            } else if (!strcmp(a, "-S")) {
                NEED();
                master_seed = strtoull(v, NULL, 0);
            } else if (!strcmp(a, "-t")) {
                NEED();
                if (ntargets == MAX_TARGETS)
                    usage();
                targets[ntargets++] = atoi(v);
            } else if (!strcmp(a, "-j")) {
                NEED();
            } else if (!strcmp(a, "-e")) {
                NEED();
                eng = v[0];
                if ((eng != 'a' && eng != 'b') || v[1])
                    usage();
            } else if (!strcmp(a, "--tg")) {
                NEED();
                tg = atoi(v);
            } else if (!strcmp(a, "--chunk")) {
                NEED();
                chunk = atol(v);
            } else if (!strcmp(a, "--amem")) {
                NEED();
                adev = !strcmp(v, "dev");
                if (!adev && strcmp(v, "tg"))
                    usage();
            } else if (!strcmp(a, "--pf")) {
                NEED();
                pf = atoi(v);
            } else if (!strcmp(a, "--gl")) {
                NEED();
                GL = atoi(v);
            } else if (!strcmp(a, "--init")) {
                NEED();
                init_path = v;
            } else if (!strcmp(a, "--final")) {
                NEED();
                final_path = v;
            } else if (!strcmp(a, "-q")) {
                quiet = 1;
            } else {
                usage();
            }
        }
        if (!gpath || !spath || R < 1 || (GL && GL < 2) || GL > 16)
            usage();
        for (int j = 1; j < ntargets; j++)
            if (targets[j] <= targets[j - 1]) {
                fprintf(stderr, "-t は小さい順に並べる\n");
                return 2;
            }
        load_graph(gpath);
        load_sched(spath);
        if (d < 2 || d > 8 || n > 65535 || n * d >= 65536) {
            fprintf(stderr, "sa_gpu: 2 ≤ d ≤ 8・n·d < 65536 だけ扱う（n=%d d=%d）\n", n, d);
            return 1;
        }
        if (GL && (1 << GL) < d) {
            fprintf(stderr, "--gl は 2^GL ≥ d にする（δ = −d..d が下の桁 + 1 桁に収まるように）\n");
            return 2;
        }

        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) {
            fprintf(stderr, "Metal のデバイスが無い\n");
            return 1;
        }
        const int K = (d + 1) / 2, NB = d < 2 ? 1 : d < 4 ? 2 : d < 8 ? 3 : 4;
        /* エンジン b で g を全部の桁で持つとき（--gl 0）の桁数: 0 ≤ g ≤ |E|。d が偶数なら g/2 を持つ。符号に 1 桁 */
        const int HALF = (d & 1) ? 0 : 1;
        int GW = 0;
        if (!GL) {
            for (int e = (n * d / 2) >> HALF; e; e >>= 1)
                GW++;
            GW++;
        }

        /* 組の大きさ: a は threadgroup（tg 本のスレッド = tg 個のレプリカ）、b は threadgroup（tg 本 × 32） */
        if (eng == 'a') {
            int maxsg = (int)(dev.maxThreadgroupMemoryLength / (4 * (NSUInteger)n));
            if (adev < 0)
                adev = maxsg < 4;
            if (adev)
                maxsg = 32; /* threadgroup メモリを使わない */
            if (maxsg < 1) {
                fprintf(stderr, "n=%d は threadgroup メモリに収まらない（--amem dev を使う）\n", n);
                return 1;
            }
            if (!tg)
                tg = 32 * (maxsg < 2 ? maxsg : 2);
            if (tg % 32 || tg / 32 > maxsg) {
                fprintf(stderr, "--tg は 32 の倍数で %d 以下\n", 32 * maxsg);
                return 1;
            }
            RP = (R + tg - 1) / tg * tg;
        } else {
            if (!tg)
                tg = 32;
            RP = (R + 32 * tg - 1) / (32 * tg) * (32 * tg);
        }
        int nth = eng == 'a' ? RP : RP / 32;
        if (pf < 0)
            pf = RP <= 2048;
        g_eng = eng;
        g_nth = nth;

        /* カーネルのコンパイル */
        NSString *defs = [NSString stringWithFormat:@"#define D %d\n#define N %d\n#define K %d\n#define NB %d\n"
                                                    @"#define NT %d\n#define GL %d\n#define GW %d\n#define APF %d\n#define ADEV %d\n",
                                                    d, n, K, NB, ntargets, GL, GW, pf, adev > 0];
        const char *extra = getenv("SA_GPU_DEFS"); /* 速さの切り分け用（例: "#define NO_RNG 1"）。結果は正しくなくなる */
        if (extra)
            defs = [defs stringByAppendingFormat:@"%s\n", extra];
        NSString *src = [defs stringByAppendingString:[[NSString alloc] initWithBytes:sa_gpu_metal
                                                                               length:sa_gpu_metal_len
                                                                             encoding:NSUTF8StringEncoding]];
        NSError *err = nil;
        MTLCompileOptions *opt = [MTLCompileOptions new];
        id<MTLLibrary> lib = [dev newLibraryWithSource:src options:opt error:&err];
        if (!lib) {
            fprintf(stderr, "コンパイルに失敗: %s\n", err.localizedDescription.UTF8String);
            return 1;
        }
        id<MTLComputePipelineState> ps_run =
            [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:(eng == 'a' ? @"sa_a" : @"sa_b")]
                                               error:&err];
        id<MTLComputePipelineState> ps_cnt =
            [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:(eng == 'a' ? @"count_a" : @"count_b")]
                                               error:&err];
        if (!ps_run || !ps_cnt) {
            fprintf(stderr, "パイプラインが作れない: %s\n", err.localizedDescription.UTF8String);
            return 1;
        }
        if (getenv("SA_GPU_INFO"))
            fprintf(stderr, "maxTotalThreadsPerThreadgroup=%lu threadExecutionWidth=%lu maxTGmem=%lu\n",
                    (unsigned long)ps_run.maxTotalThreadsPerThreadgroup, (unsigned long)ps_run.threadExecutionWidth,
                    (unsigned long)dev.maxThreadgroupMemoryLength);
        if ((NSUInteger)tg > ps_run.maxTotalThreadsPerThreadgroup) {
            fprintf(stderr, "--tg %d はこのカーネルの上限 %lu を超える\n", tg,
                    (unsigned long)ps_run.maxTotalThreadsPerThreadgroup);
            return 1;
        }

        init_replicas();

        /* 共通のバッファ */
        id<MTLBuffer> b_adj = buf(dev, sizeof(uint16_t) * n * d);
        for (int i = 0; i < n * d; i++)
            ((uint16_t *)b_adj.contents)[i] = (uint16_t)adj[i];
        id<MTLBuffer> b_adjm = buf(dev, sizeof(uint32_t) * n * d); /* エンジン b: 隣の番号 × nth */
        for (int i = 0; i < n * d; i++)
            ((uint32_t *)b_adjm.contents)[i] = adj[i] * (uint32_t)nth;
        id<MTLBuffer> b_th = buf(dev, sizeof(uint32_t) * S * (d + 1));
        memcpy(b_th.contents, th, sizeof(uint32_t) * S * (d + 1));
        id<MTLBuffer> b_tgt = buf(dev, sizeof(int32_t) * MAX_TARGETS);
        memcpy(b_tgt.contents, targets, sizeof(int32_t) * ntargets);
        id<MTLBuffer> b_zs = buf(dev, sizeof(uint32_t) * S);
        for (int k = 0; k < S; k++) {
            uint32_t o = 0;
            for (int q = 0; q < K; q++)
                o |= th[k * (d + 1) + q];
            ((uint32_t *)b_zs.contents)[k] = o ? (uint32_t)__builtin_clz(o) : 32;
        }
        id<MTLBuffer> b_col = buf(dev, sizeof(uint32_t) * (size_t)n * (eng == 'a' ? RP / 32 : nth));
        id<MTLBuffer> b_cnt = buf(dev, sizeof(int32_t) * RP);
        id<MTLBuffer> b_bt = buf(dev, sizeof(uint64_t) * RP);
        id<MTLBuffer> b_hit = buf(dev, sizeof(uint64_t) * (size_t)RP * (ntargets ? ntargets : 1));
        memset(b_bt.contents, 0xFF, b_bt.length);
        memset(b_hit.contents, 0xFF, b_hit.length);

        /* 塗り分けを GPU の形に（32 レプリカ × 32 頂点ずつ転置） */
        uint32_t *col = b_col.contents;
        for (int p0 = 0; p0 < RP; p0 += 32)
            for (int v0 = 0; v0 < n; v0 += 32) {
                uint32_t blk[32];
                pack32(p0, v0, blk);
                for (int j = 0; j < 32 && v0 + j < n; j++) {
                    if (eng == 'a')
                        col[(size_t)(p0 / 32) * n + v0 + j] = blk[j];
                    else
                        col[(size_t)(v0 + j) * nth + p0 / 32] = blk[j];
                }
            }

        id<MTLCommandQueue> queue = [dev newCommandQueue];
        params_t prm = {0, 0, 0, 0, (uint32_t)nth, 0};

        /* 最初のカット値 */
        void (^count)(void) = ^{
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:ps_cnt];
            [ce setBuffer:b_col offset:0 atIndex:0];
            if (eng == 'a') {
                [ce setBuffer:b_adj offset:0 atIndex:5];
                [ce setBuffer:b_cnt offset:0 atIndex:10];
            } else {
                [ce setBuffer:b_adjm offset:0 atIndex:8];
                [ce setBytes:&prm length:sizeof prm atIndex:12];
                [ce setBuffer:b_cnt offset:0 atIndex:14];
            }
            [ce dispatchThreads:MTLSizeMake(nth, 1, 1) threadsPerThreadgroup:MTLSizeMake(eng == 'a' ? 32 : 32, 1, 1)];
            [ce endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.error) {
                fprintf(stderr, "GPU のエラー: %s\n", cb.error.localizedDescription.UTF8String);
                exit(1);
            }
        };
        count();
        int32_t *c0 = malloc(sizeof(int32_t) * RP);
        memcpy(c0, b_cnt.contents, sizeof(int32_t) * RP);

        /* エンジンごとの状態 */
        id<MTLBuffer> b_xs = nil, b_st = nil, b_xb = nil, b_gp = nil, b_hh = nil, b_best = nil, b_nx = nil;
        if (eng == 'a') {
            b_xs = buf(dev, sizeof(uint32_t) * 2 * RP);
            b_st = buf(dev, sizeof(int32_t) * 4 * RP);
            uint32_t *xs = b_xs.contents;
            int32_t *st = b_st.contents;
            for (int p = 0; p < RP; p++) {
                xs[2 * p] = (uint32_t)seeds[p];
                xs[2 * p + 1] = (uint32_t)(seeds[p] >> 32);
                st[4 * p] = c0[p];
                st[4 * p + 1] = c0[p];
            }
        } else {
            b_xb = buf(dev, sizeof(uint32_t) * 64 * nth);
            b_gp = buf(dev, sizeof(uint32_t) * (GW ? GW : GL) * nth);
            b_hh = buf(dev, sizeof(int32_t) * RP);
            b_best = buf(dev, sizeof(int32_t) * RP);
            b_nx = buf(dev, sizeof(int32_t) * RP);
            uint32_t *xb = b_xb.contents;
            for (int q = 0; q < nth; q++)
                for (int i = 0; i < 32; i++) {
                    uint64_t s = seeds[q * 32 + i];
                    for (int b = 0; b < 64; b++)
                        xb[b * nth + q] |= (uint32_t)((s >> b) & 1) << i;
                }
            memcpy(b_best.contents, c0, sizeof(int32_t) * RP);
        }

        /* 段と周回数の組を、dispatch ごとの区間に分ける（1 dispatch の周回数は chunk 以下） */
        if (chunk <= 0)
            chunk = (1L << 20) / n > 0 ? (1L << 20) / n : 1;
        if ((uint64_t)chunk * n >= (1ull << 31)) /* dispatch の中の試行番号は 32 bit（全部 1 は「無し」の印） */
            chunk = (long)((1ull << 31) / n);
        uint32_t *segs = malloc(sizeof(uint32_t) * 2 * (S + total_sweeps / chunk + 2) * 2);
        int nseg = 0;
        int *cut = malloc(sizeof(int) * (S + total_sweeps / chunk + 4) * 2); /* dispatch の境目（組の番号） */
        int ncut = 0;
        cut[ncut++] = 0;
        long inchunk = 0;
        for (int k = 0; k < S; k++) {
            uint32_t left = sweeps[k];
            while (left > 0) {
                uint32_t take = (uint32_t)(left < (uint32_t)(chunk - inchunk) ? left : (uint32_t)(chunk - inchunk));
                segs[2 * nseg] = (uint32_t)k;
                segs[2 * nseg + 1] = take;
                nseg++;
                left -= take;
                inchunk += take;
                if (inchunk == chunk) {
                    cut[ncut++] = nseg;
                    inchunk = 0;
                }
            }
        }
        if (cut[ncut - 1] != nseg)
            cut[ncut++] = nseg;
        id<MTLBuffer> b_seg = buf(dev, sizeof(uint32_t) * 2 * (nseg ? nseg : 1));
        memcpy(b_seg.contents, segs, sizeof(uint32_t) * 2 * nseg);

        /* 実行。dispatch ごとに command buffer を分け、全部送ってから最後を待つ */
        NSUInteger tgmem = eng == 'a' && !adev ? (NSUInteger)(tg / 32) * n * 4 : 0;
        NSMutableArray *cbs = [NSMutableArray array];
        double t0 = now();
        uint64_t tbase = 0;
        for (int ci = 0; ci + 1 < ncut; ci++) {
            params_t pr = prm;
            pr.seg0 = (uint32_t)cut[ci];
            pr.seg1 = (uint32_t)cut[ci + 1];
            pr.tb_lo = (uint32_t)tbase;
            pr.tb_hi = (uint32_t)(tbase >> 32);
            for (int s = cut[ci]; s < cut[ci + 1]; s++)
                tbase += (uint64_t)segs[2 * s + 1] * n;
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:ps_run];
            if (eng == 'a') {
                [ce setBuffer:b_col offset:0 atIndex:0];
                [ce setBuffer:b_xs offset:0 atIndex:1];
                [ce setBuffer:b_st offset:0 atIndex:2];
                [ce setBuffer:b_bt offset:0 atIndex:3];
                [ce setBuffer:b_hit offset:0 atIndex:4];
                [ce setBuffer:b_adj offset:0 atIndex:5];
                [ce setBuffer:b_th offset:0 atIndex:6];
                [ce setBuffer:b_seg offset:0 atIndex:7];
                [ce setBuffer:b_tgt offset:0 atIndex:8];
                [ce setBytes:&pr length:sizeof pr atIndex:9];
                [ce setThreadgroupMemoryLength:tgmem atIndex:0];
            } else {
                [ce setBuffer:b_col offset:0 atIndex:0];
                [ce setBuffer:b_xb offset:0 atIndex:1];
                [ce setBuffer:b_gp offset:0 atIndex:2];
                [ce setBuffer:b_hh offset:0 atIndex:3];
                [ce setBuffer:b_best offset:0 atIndex:4];
                [ce setBuffer:b_nx offset:0 atIndex:5];
                [ce setBuffer:b_bt offset:0 atIndex:6];
                [ce setBuffer:b_hit offset:0 atIndex:7];
                [ce setBuffer:b_adjm offset:0 atIndex:8];
                [ce setBuffer:b_th offset:0 atIndex:9];
                [ce setBuffer:b_seg offset:0 atIndex:10];
                [ce setBuffer:b_tgt offset:0 atIndex:11];
                [ce setBytes:&pr length:sizeof pr atIndex:12];
                [ce setBuffer:b_zs offset:0 atIndex:13];
            }
            [ce dispatchThreads:MTLSizeMake(nth, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
            [ce endEncoding];
            [cb commit];
            [cbs addObject:cb];
        }
        [(id<MTLCommandBuffer>)cbs.lastObject waitUntilCompleted];
        double sec = now() - t0;
        double gpu_sec = 0;
        for (id<MTLCommandBuffer> cb in cbs) {
            [cb waitUntilCompleted];
            if (cb.error) {
                fprintf(stderr, "GPU のエラー: %s\n", cb.error.localizedDescription.UTF8String);
                return 1;
            }
            gpu_sec += cb.GPUEndTime - cb.GPUStartTime;
        }

        /* 最後のカット値を数え直し、追いかけてきた値と突き合わせる */
        count();
        const int32_t *cnt = b_cnt.contents;
        int32_t *best = malloc(sizeof(int32_t) * RP), *fin = malloc(sizeof(int32_t) * RP);
        if (eng == 'a') {
            const int32_t *st = b_st.contents;
            for (int p = 0; p < RP; p++) {
                fin[p] = st[4 * p];
                best[p] = st[4 * p + 1];
            }
        } else {
            const uint32_t *gp = b_gp.contents;
            const int32_t *hh = b_hh.contents;
            memcpy(best, b_best.contents, sizeof(int32_t) * RP);
            for (int p = 0; p < RP; p++) {
                int32_t lo = 0;
                for (int j = 0; j < (GW ? GW : GL); j++)
                    lo |= (int32_t)((gp[j * nth + p / 32] >> (p % 32)) & 1) << j;
                fin[p] = GW ? best[p] - (lo << HALF) : best[p] - ((hh[p] << GL) + lo);
            }
        }
        for (int p = 0; p < RP && !extra; p++)
            if (cnt[p] != fin[p]) {
                fprintf(stderr, "レプリカ %d: 足し込んだカット値 %d と数え直した値 %d が違う\n", p, fin[p], cnt[p]);
                return 1;
            }
        const uint64_t *bt = b_bt.contents, *hit = b_hit.contents;

        printf("# n=%d d=%d edges=%d R=%d seed=%" PRIu64 " stages=%d sweeps=%" PRIu64 " order=seq rng=xs64\n", n,
               d, n * d / 2, R, master_seed, S, total_sweeps);
        if (!quiet) {
            printf("# p seed c0 best best_t final");
            for (int j = 0; j < ntargets; j++)
                printf(" hit_t[%d]", targets[j]);
            printf("\n");
            for (int p = 0; p < R; p++) {
                printf("%d %016" PRIx64 " %d %d", p, seeds[p], c0[p], best[p]);
                print_t(bt[p]);
                printf(" %d", fin[p]);
                for (int j = 0; j < ntargets; j++)
                    print_t(hit[(size_t)p * ntargets + j]);
                printf("\n");
            }
        }
        long long sum = 0;
        int32_t mx = best[0], mn = best[0];
        for (int p = 0; p < R; p++) {
            sum += best[p];
            if (best[p] > mx)
                mx = best[p];
            if (best[p] < mn)
                mn = best[p];
        }
        printf("summary best: mean=%.3f min=%d max=%d\n", (double)sum / R, mn, mx);
        for (int j = 0; j < ntargets; j++) {
            int hits = 0;
            for (int p = 0; p < R; p++)
                hits += hit[(size_t)p * ntargets + j] != NONE;
            printf("summary target %d: %d/%d\n", targets[j], hits, R);
        }

        double trials = (double)R * total_sweeps * n;
        double trials_pad = (double)RP * total_sweeps * n;
        fprintf(stderr,
                "time %.3f s, %.3g trials/s (engine=%c R=%d padded=%d tg=%d dispatches=%d gpu=%.3f s, %.3g trials/s padded)\n",
                sec, trials / sec, eng, R, RP, tg, ncut - 1, gpu_sec, trials_pad / sec);

        if (init_path)
            dump_colors(init_path, color0);
        if (final_path) {
            g_col = b_col.contents;
            dump_colors(final_path, color_final);
        }
    }
    return 0;
}
