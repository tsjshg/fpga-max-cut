/*
 * sa.c — 重みなし Max-Cut の焼きなまし（CPU 版の正解）
 *
 * 回路と同じ規則で動き、整数だけを使うので、Mac・ボード・回路がビット単位で同じ道筋をたどる。
 * 規則（docs/design-draft.md の「2. アルゴリズム」と同じ）:
 *
 *   レプリカ p の試行 t = 0, 1, 2, …（周回 k = t / n、頂点 v = t % n）
 *     x ← xorshift64(x)、r = x >> 32           … 乱数は ΔC によらず毎試行1回進める
 *     n_same = |{u ∈ adj[v] : s[u] = s[v]}|、ΔC = 2·n_same − d
 *     受け入れ = (ΔC ≥ 0) または (r < TH[段][n_same])
 *     受け入れたら s[v] を反転、C += ΔC
 *       C > best（等しいときは更新しない）なら best = C、best_t = t
 *       C ≥ target[j] にはじめてなったら hit_t[j] = t（目標は初期のカット値より大きくとる。
 *       そうすれば「はじめて届く」のは必ず best の更新と同時なので、回路では best の更新の
 *       ところで比べればよい）
 *
 *   種: 主種から始まる splitmix64 の列を 2^32 個ずつの区間に分け、レプリカ p は p 番目の
 *       区間を使う。最初の1個（0 なら次）が xorshift64 の種、続く ⌈n/64⌉ 個が初期の塗り分け
 *       （頂点 v の色 = 語 v/64 のビット v%64）。こうするとレプリカ数 R を変えても
 *       レプリカ p の道筋は変わらない。
 *
 * 比較用の別の規則（回路には入れない。解の質の比較だけに使う）:
 *   --order rand : 頂点を順に回さず、毎試行ランダムに選ぶ（乱数を1試行2回引く。先の1回で v）
 *   --rng sm64   : xorshift64 の代わりに splitmix64 を使う（乱数の質の確認用）
 *
 * 標準出力には決定的な結果だけを出す（Mac とボードで diff できる）。時間は標準エラーへ。
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static inline uint64_t xs64(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

/* ---- 入力 ---- */

static int n, d;
static uint32_t *adj;          /* n*d */
static int S;                  /* 温度の段数 */
static uint32_t *sweeps;       /* S */
static uint32_t *th;           /* S*(d+1) */
static uint64_t total_sweeps;

static int ntargets;
static int32_t targets[MAX_TARGETS];
static uint64_t master_seed = 1;
static int order_rand, rng_sm64;

/* ---- 結果 ---- */

typedef struct {
    uint64_t seed;             /* xorshift64 の種（回路に渡すもの） */
    int32_t c0, best, final;
    uint64_t best_t;
    uint64_t hit_t[MAX_TARGETS];
} result_t;

static int R;
static result_t *res;
static uint8_t *colors0, *colors; /* R*n（初期と最後の塗り分け） */

static FILE *xopen(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    if (!f) {
        perror(path);
        exit(1);
    }
    return f;
}

/* '#' で始まる行を飛ばして整数を1つ読む */
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
    /* 対称性の確認（u が v の隣なら v も u の隣） */
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

static int32_t cut_value(const uint8_t *s)
{
    int32_t c = 0;
    for (int v = 0; v < n; v++)
        for (int i = 0; i < d; i++)
            c += s[v] != s[adj[v * d + i]];
    return c / 2;
}

/* レプリカ p の初期化（種と初期の塗り分け） */
static void init_replica(int p, uint64_t *xs, uint64_t *smstate, uint8_t *s)
{
    uint64_t st = master_seed + ((uint64_t)p << 32) * GOLDEN;
    uint64_t seed;
    do
        seed = sm64(&st);
    while (seed == 0);
    uint64_t w = 0;
    for (int v = 0; v < n; v++) {
        if (v % 64 == 0)
            w = sm64(&st);
        s[v] = (w >> (v % 64)) & 1;
    }
    *xs = seed;
    *smstate = seed; /* --rng sm64 のときはこの値から splitmix64 を回す */
    res[p].seed = seed;
}

static inline uint32_t draw(uint64_t *x)
{
    return (uint32_t)((rng_sm64 ? sm64(x) : xs64(x)) >> 32);
}

static void run_replica(int p)
{
    uint8_t *s = colors + (size_t)p * n;
    uint64_t x, smstate;
    init_replica(p, &x, &smstate, s);
    if (rng_sm64)
        x = smstate;
    memcpy(colors0 + (size_t)p * n, s, n);

    result_t *rp = &res[p];
    int32_t c = cut_value(s);
    rp->c0 = c;
    int32_t best = c;
    uint64_t best_t = NONE;
    uint64_t hit_t[MAX_TARGETS];
    int next_target = 0; /* targets は昇順。まだ届いていない最小の番号 */
    for (int j = 0; j < MAX_TARGETS; j++)
        hit_t[j] = NONE;

    uint64_t t = 0;
    for (int k = 0; k < S; k++) {
        const uint32_t *T = th + k * (d + 1);
        for (uint32_t m = 0; m < sweeps[k]; m++)
        for (int vi = 0; vi < n; vi++, t++) {
            int v = vi;
            if (order_rand)
                v = (int)(((uint64_t)draw(&x) * (uint64_t)n) >> 32);
            uint32_t r = draw(&x);
            const uint32_t *nb = adj + v * d;
            uint8_t sv = s[v];
            int ns = 0;
            for (int j = 0; j < d; j++)
                ns += s[nb[j]] == sv;
            int dc = 2 * ns - d;
            if (dc >= 0 || r < T[ns]) {
                s[v] = sv ^ 1;
                c += dc;
                if (c > best) {
                    best = c;
                    best_t = t;
                    while (next_target < ntargets && c >= targets[next_target])
                        hit_t[next_target++] = t;
                }
            }
        }
    }
    rp->best = best;
    rp->best_t = best_t;
    rp->final = c;
    memcpy(rp->hit_t, hit_t, sizeof hit_t);
    if (cut_value(s) != c) {
        fprintf(stderr, "レプリカ %d: 足し込んだカット値 %d と数え直した値 %d が違う\n", p, c,
                cut_value(s));
        exit(1);
    }
}

static int nthreads = 1;

static void *worker(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int p = id; p < R; p += nthreads)
        run_replica(p);
    return NULL;
}

static void dump_colors(const char *path, const uint8_t *buf)
{
    FILE *f = xopen(path, "w");
    for (int p = 0; p < R; p++) {
        for (int v = 0; v < n; v++)
            fputc('0' + buf[(size_t)p * n + v], f);
        fputc('\n', f);
    }
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
            "使い方: sa -g graph.adj -s sched.sched [-R レプリカ数] [-S 主種] [-t 目標]...\n"
            "          [-j スレッド数] [--order seq|rand] [--rng xs64|sm64]\n"
            "          [--init 初期の塗り分けの書き出し先] [--final 最後の塗り分けの書き出し先]\n"
            "          [-q]（レプリカごとの行を出さず、まとめだけ）\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *gpath = NULL, *spath = NULL, *init_path = NULL, *final_path = NULL;
    int quiet = 0;
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
            nthreads = atoi(v);
        } else if (!strcmp(a, "--order")) {
            NEED();
            order_rand = !strcmp(v, "rand");
            if (!order_rand && strcmp(v, "seq"))
                usage();
        } else if (!strcmp(a, "--rng")) {
            NEED();
            rng_sm64 = !strcmp(v, "sm64");
            if (!rng_sm64 && strcmp(v, "xs64"))
                usage();
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
    if (!gpath || !spath || R < 1 || nthreads < 1)
        usage();
    for (int j = 1; j < ntargets; j++)
        if (targets[j] <= targets[j - 1]) {
            fprintf(stderr, "-t は小さい順に並べる\n");
            return 2;
        }

    load_graph(gpath);
    load_sched(spath);
    res = calloc(R, sizeof *res);
    colors0 = malloc((size_t)R * n);
    colors = malloc((size_t)R * n);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (nthreads == 1) {
        worker(0);
    } else {
        pthread_t *th_ = malloc(sizeof(pthread_t) * nthreads);
        for (int i = 0; i < nthreads; i++)
            pthread_create(&th_[i], NULL, worker, (void *)(intptr_t)i);
        for (int i = 0; i < nthreads; i++)
            pthread_join(th_[i], NULL);
        free(th_);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double sec = (t1.tv_sec - t0.tv_sec) + 1e-9 * (t1.tv_nsec - t0.tv_nsec);

    printf("# n=%d d=%d edges=%d R=%d seed=%" PRIu64 " stages=%d sweeps=%" PRIu64
           " order=%s rng=%s\n",
           n, d, n * d / 2, R, master_seed, S, total_sweeps, order_rand ? "rand" : "seq",
           rng_sm64 ? "sm64" : "xs64");
    if (!quiet) {
        printf("# p seed c0 best best_t final");
        for (int j = 0; j < ntargets; j++)
            printf(" hit_t[%d]", targets[j]);
        printf("\n");
        for (int p = 0; p < R; p++) {
            result_t *rp = &res[p];
            printf("%d %016" PRIx64 " %d %d", p, rp->seed, rp->c0, rp->best);
            print_t(rp->best_t);
            printf(" %d", rp->final);
            for (int j = 0; j < ntargets; j++)
                print_t(rp->hit_t[j]);
            printf("\n");
        }
    }
    /* まとめ（決定的） */
    long long sum = 0;
    int32_t mx = res[0].best, mn = res[0].best;
    for (int p = 0; p < R; p++) {
        sum += res[p].best;
        if (res[p].best > mx)
            mx = res[p].best;
        if (res[p].best < mn)
            mn = res[p].best;
    }
    printf("summary best: mean=%.3f min=%d max=%d\n", (double)sum / R, mn, mx);
    for (int j = 0; j < ntargets; j++) {
        int hits = 0;
        for (int p = 0; p < R; p++)
            hits += res[p].hit_t[j] != NONE;
        printf("summary target %d: %d/%d\n", targets[j], hits, R);
    }

    double trials = (double)R * total_sweeps * n;
    fprintf(stderr, "time %.3f s, %.3g trials/s (threads=%d)\n", sec, trials / sec, nthreads);

    if (init_path)
        dump_colors(init_path, colors0);
    if (final_path)
        dump_colors(final_path, colors);
    return 0;
}
