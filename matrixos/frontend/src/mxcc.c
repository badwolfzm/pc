/*
 * mxcc -- MatrixOS compiler driver.
 *
 * Replaces matrixos_faststripcc_mini_swrap (the uploaded 104-line wrapper).
 * It composes a command line for clang or gcc from four independent axes and
 * execs it:
 *
 *   profile   compile-fast | matrixos | balanced | runtime-fast   (optimisation shape)
 *   tier      baseline | avx2 | avx512                            (fixed SIMD level)
 *   policy    hardening + reproducibility + warning sets          (on by default)
 *   mode      -E | -S | -c | link                                 (what to produce)
 *
 * Rules carried over from the design doc and the control plan:
 *   - No -march=native. Tiers are explicit psABI levels so builds are
 *     reproducible across machines. Passing -march=native is an error unless
 *     --allow-native is given.
 *   - Every buffer has a hard ceiling. Argument vectors are fixed-size and an
 *     overflow is a fatal error, never a reallocation past the limit.
 *   - --typed-input injects include/mx_input.h and promotes the conversion
 *     warnings that make the input/control type separation enforceable.
 *   - --dry-run prints the exact argv (one argument per line) and exits 0, so
 *     the test suite can assert on command lines without a compiler present.
 *   - Security review finding 3: flags that make the compiler load code at
 *     compile time (-fplugin, -fpass-plugin, -Xclang, -load, -specs, -B,
 *     -wrapper, @file, -Wp,/-Wa, and linker plugins) are rejected, the
 *     compiler is resolved against a fixed directory list (never $PATH), run
 *     with execve on an absolute path and a minimal environment.
 *   - --emit-build-info writes the BUILD_INFO lines the MXSB v2 contract
 *     expects (profile, simd_tier, source_sha256, toolchain).
 *
 * Build: cc -std=c11 -O2 -Wall -Wextra -o mxcc src/mxcc.c
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#define MXCC_VERSION "1.0.0"
#define MXCC_MAX_ARGS 4096          /* hard ceiling on the composed argv */
#define MXCC_MAX_PATH 4096

typedef enum { PROF_COMPILE_FAST, PROF_MATRIXOS, PROF_BALANCED, PROF_RUNTIME_FAST } profile_t;
typedef enum { TIER_BASELINE, TIER_AVX2, TIER_AVX512 } tier_t;
typedef enum { ARCH_X86_64, ARCH_AARCH64 } arch_t;
typedef enum { MODE_LINK, MODE_COMPILE, MODE_ASM, MODE_PREPROCESS } mode_t_;

typedef struct {
    const char *cc;             /* "clang" | "gcc" | explicit path ending in one of those */
    bool cc_is_clang;
    profile_t profile;
    tier_t tier;
    arch_t arch;
    mode_t_ mode;
    bool harden, reproducible, typed_input, strict, freestanding, allow_native, dry_run;
    bool debug;                 /* -g seen: do not strip */
    const char *out;
    const char *mx_include_dir; /* where mx_input.h lives */
    const char *sysroot;
    const char *target;         /* clang --target= passthrough */
    const char *build_info;     /* --emit-build-info=FILE */
    bool keep_env;
    char cc_abs[MXCC_MAX_PATH]; /* resolved absolute compiler path */
    const char *inputs[MXCC_MAX_ARGS];
    int ninputs;
    const char *extra[MXCC_MAX_ARGS];   /* passthrough flags, in order */
    int nextra;
} opts_t;

typedef struct {
    const char *v[MXCC_MAX_ARGS + 1];
    int n;
} argv_t;

static void die(const char *fmt, ...) {
    va_list ap;
    fputs("mxcc: error: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(2);
}

static void push(argv_t *a, const char *s) {
    if (a->n >= MXCC_MAX_ARGS) die("argument vector exceeds hard ceiling of %d", MXCC_MAX_ARGS);
    a->v[a->n++] = s;
}

/* push a formatted argument; storage is a small bounded pool */
static const char *fmt_arg(const char *fmt, ...) {
    static char pool[64][MXCC_MAX_PATH];
    static int used = 0;
    if (used >= 64) die("too many formatted arguments");
    char *buf = pool[used++];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, MXCC_MAX_PATH, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= MXCC_MAX_PATH) die("argument longer than %d bytes", MXCC_MAX_PATH);
    return buf;
}

static bool eq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static bool pfx(const char *a, const char *p) { return strncmp(a, p, strlen(p)) == 0; }

static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void usage(FILE *f) {
    fputs(
"usage: mxcc [options] file...\n"
"\n"
"  --cc=clang|gcc           compiler to drive (env MXCC_CC; default clang)\n"
"  --profile=NAME           compile-fast | matrixos (default) | balanced | runtime-fast\n"
"  --tier=NAME              baseline (default) | avx2 | avx512   fixed SIMD level, no -march=native\n"
"  --arch=x86-64|aarch64    target architecture (default x86-64)\n"
"  --target=TRIPLE          clang cross target passthrough\n"
"  --sysroot=DIR            sysroot passthrough\n"
"  --typed-input            include mx_input.h and enforce the input/control type separation\n"
"  --strict                 promote the policy warnings (vla, alloca, conversions) to errors\n"
"  --freestanding           Warden-ABI style: -ffreestanding -nostdlib -nostdinc -static\n"
"  --no-harden              drop the hardening flag set\n"
"  --no-reproducible        drop the reproducibility flag set\n"
"  --allow-native           permit -march=native (off: it is an error)\n"
"  --mx-include=DIR         directory holding mx_input.h (env MXCC_INCLUDE)\n"
"  --emit-build-info=FILE   write MXSB BUILD_INFO key=value lines (profile, simd_tier, source_sha256, toolchain)\n"
"  --keep-env               pass the caller's environment to the compiler (default: minimal env)\n"
"  --dry-run                print the composed command, one argument per line, and exit\n"
"  -E | -S | -c             preprocess / assemble / compile only (default: link)\n"
"  -o FILE                  output file\n"
"  --                       everything after is passed through untouched\n"
"  --version, --help\n",
    f);
}


static const char *tier_name(tier_t t);

/* ---- security review finding 3: no code loading via flags ------------ */
static bool is_denied_flag(const char *a) {
    static const char *const prefixes[] = {
        "-fplugin", "-fpass-plugin", "-Xclang", "-load", "-specs", "-B", "-wrapper", "@",
        "-Wp,", "-Wa,", "-fsanitize-coverage=", "-fprofile-use", "-fprofile-instr-use",
        "-fdebug-compilation-dir", "-ftime-trace", "-save-temps", "-dumpdir", "-dumpbase",
        "-fplugin-arg", "-print-prog-name", "-isysroot",
        NULL
    };
    for (int i = 0; prefixes[i]; i++) if (pfx(a, prefixes[i])) return true;
    /* linker: plugins and arbitrary linker scripts */
    if (pfx(a, "-Wl,") || eq(a, "-Xlinker")) {
        if (strstr(a, "plugin") || strstr(a, "-T") || strstr(a, "--script") || strstr(a, "@")
            || strstr(a, "-rpath") || strstr(a, "--dynamic-linker") || strstr(a, "-z,lazy")) return true;
    }
    if (eq(a, "-T") || pfx(a, "-T")) return true;
    return false;
}

/* Resolve the compiler to an absolute path from a fixed list; never $PATH. */
static void resolve_compiler(opts_t *o) {
    if (o->cc[0] == '/') {
        if (strlen(o->cc) >= sizeof(o->cc_abs)) die("compiler path too long");
        strcpy(o->cc_abs, o->cc);
    } else {
        if (strchr(o->cc, '/')) die("--cc must be a bare name (clang, gcc) or an absolute path");
        static const char *const dirs[] = { "/usr/bin", "/usr/local/bin", "/bin", "/usr/lib/llvm-18/bin", NULL };
        bool found = false;
        for (int i = 0; dirs[i] && !found; i++) {
            int n = snprintf(o->cc_abs, sizeof(o->cc_abs), "%s/%s", dirs[i], o->cc);
            if (n < 0 || (size_t)n >= sizeof(o->cc_abs)) die("compiler path too long");
            found = access(o->cc_abs, X_OK) == 0;
        }
        if (!found) die("compiler '%s' not found in /usr/bin, /usr/local/bin or /bin (give an absolute path)", o->cc);
    }
    struct stat st;
    if (stat(o->cc_abs, &st) != 0 || !S_ISREG(st.st_mode) || (st.st_mode & S_IWOTH))
        die("compiler '%s' is not a regular, non-world-writable file", o->cc_abs);
}

/* ---- SHA-256 (FIPS 180-4) for BUILD_INFO source_sha256 ---------------- */
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } sha256_t;
static uint32_t ror(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
static void sha256_block(sha256_t *s, const uint8_t *p) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ror(e,6) ^ ror(e,11) ^ ror(e,25), ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ror(a,2) ^ ror(a,13) ^ ror(a,22), maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    s->h[0]+=a; s->h[1]+=b; s->h[2]+=c; s->h[3]+=d; s->h[4]+=e; s->h[5]+=f; s->h[6]+=g; s->h[7]+=h;
}
static void sha256_init(sha256_t *s) {
    static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->h, iv, sizeof iv); s->len = 0; s->n = 0;
}
static void sha256_update(sha256_t *s, const void *data, size_t n) {
    const uint8_t *p = data;
    s->len += n;
    while (n) {
        size_t k = 64 - s->n; if (k > n) k = n;
        memcpy(s->buf + s->n, p, k); s->n += k; p += k; n -= k;
        if (s->n == 64) { sha256_block(s, s->buf); s->n = 0; }
    }
}
static void sha256_final(sha256_t *s, uint8_t out[32]) {
    uint64_t bits = s->len * 8;
    uint8_t pad = 0x80; sha256_update(s, &pad, 1);
    uint8_t z = 0; while (s->n != 56) sha256_update(s, &z, 1);
    uint8_t lb[8]; for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8*i));
    sha256_update(s, lb, 8);
    for (int i = 0; i < 8; i++) { out[4*i] = (uint8_t)(s->h[i] >> 24); out[4*i+1] = (uint8_t)(s->h[i] >> 16); out[4*i+2] = (uint8_t)(s->h[i] >> 8); out[4*i+3] = (uint8_t)s->h[i]; }
}

/* BUILD_INFO: key=value lines per the MXSB v2 contract section 3.3. source_sha256
 * is SHA-256 over the input files concatenated in command-line order. */
static void emit_build_info(const opts_t *o, const argv_t *a) {
    sha256_t s; sha256_init(&s);
    uint8_t buf[65536];
    for (int i = 0; i < o->ninputs; i++) {
        FILE *f = fopen(o->inputs[i], "rb");
        if (!f) die("cannot read '%s' for source_sha256: %s", o->inputs[i], strerror(errno));
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) sha256_update(&s, buf, n);
        if (ferror(f)) die("read error on '%s'", o->inputs[i]);
        fclose(f);
    }
    uint8_t d[32]; sha256_final(&s, d);
    FILE *f = fopen(o->build_info, "w");
    if (!f) die("cannot write '%s': %s", o->build_info, strerror(errno));
    fprintf(f, "profile=%s\n", o->profile == PROF_COMPILE_FAST ? "compile-fast" : o->profile == PROF_MATRIXOS ? "matrixos" :
                                 o->profile == PROF_BALANCED ? "balanced" : "runtime-fast");
    fprintf(f, "simd_tier=%s\n", tier_name(o->tier));
    fprintf(f, "source_sha256=");
    for (int i = 0; i < 32; i++) fprintf(f, "%02x", d[i]);
    fprintf(f, "\ntoolchain=%s\n", o->cc_abs);
    fprintf(f, "driver=mxcc %s\n", MXCC_VERSION);
    fprintf(f, "typed_input=%d\nhardened=%d\nreproducible=%d\n", o->typed_input, o->harden, o->reproducible);
    fprintf(f, "flags=");
    for (int i = 1; i < a->n; i++) fprintf(f, "%s%s", i > 1 ? " " : "", a->v[i]);
    fputc('\n', f);
    if (fclose(f) != 0) die("write error on '%s'", o->build_info);
}

static void parse_args(int argc, char **argv, opts_t *o) {
    memset(o, 0, sizeof(*o));
    o->cc = getenv("MXCC_CC") ? getenv("MXCC_CC") : "clang";
    o->profile = PROF_MATRIXOS;
    o->tier = TIER_BASELINE;
    o->arch = ARCH_X86_64;
    o->mode = MODE_LINK;
    o->harden = true;
    o->reproducible = true;
    o->mx_include_dir = getenv("MXCC_INCLUDE");

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        #define NEEDARG() do { if (i + 1 >= argc) die("missing argument after %s", a); } while (0)
        if (eq(a, "--")) {
            while (++i < argc) {
                if (o->nextra >= MXCC_MAX_ARGS) die("too many arguments");
                o->extra[o->nextra++] = argv[i];
            }
            break;
        } else if (eq(a, "--help") || eq(a, "-h")) {
            usage(stdout); exit(0);
        } else if (eq(a, "--version")) {
            printf("mxcc %s (MatrixOS compiler driver)\n", MXCC_VERSION); exit(0);
        } else if (eq(a, "--cc")) { NEEDARG(); o->cc = argv[++i];
        } else if (pfx(a, "--cc=")) { o->cc = a + 5;
        } else if (eq(a, "--profile")) { NEEDARG(); a = argv[++i]; goto profile;
        } else if (pfx(a, "--profile=")) { a += 10;
        profile:
            if (eq(a, "compile-fast")) o->profile = PROF_COMPILE_FAST;
            else if (eq(a, "matrixos")) o->profile = PROF_MATRIXOS;
            else if (eq(a, "balanced")) o->profile = PROF_BALANCED;
            else if (eq(a, "runtime-fast")) o->profile = PROF_RUNTIME_FAST;
            else die("unsupported profile '%s' (compile-fast|matrixos|balanced|runtime-fast)", a);
        } else if (eq(a, "--tier")) { NEEDARG(); a = argv[++i]; goto tier;
        } else if (pfx(a, "--tier=")) { a += 7;
        tier:
            if (eq(a, "baseline")) o->tier = TIER_BASELINE;
            else if (eq(a, "avx2")) o->tier = TIER_AVX2;
            else if (eq(a, "avx512")) o->tier = TIER_AVX512;
            else die("unsupported tier '%s' (baseline|avx2|avx512)", a);
        } else if (pfx(a, "--arch=")) {
            a += 7;
            if (eq(a, "x86-64") || eq(a, "x86_64")) o->arch = ARCH_X86_64;
            else if (eq(a, "aarch64") || eq(a, "arm64")) o->arch = ARCH_AARCH64;
            else die("unsupported arch '%s' (x86-64|aarch64)", a);
        } else if (pfx(a, "--target=")) { o->target = a + 9;
        } else if (pfx(a, "--sysroot=")) { o->sysroot = a + 10;
        } else if (eq(a, "--typed-input")) { o->typed_input = true;
        } else if (eq(a, "--strict")) { o->strict = true;
        } else if (eq(a, "--freestanding")) { o->freestanding = true;
        } else if (eq(a, "--no-harden")) { o->harden = false;
        } else if (eq(a, "--no-reproducible")) { o->reproducible = false;
        } else if (eq(a, "--allow-native")) { o->allow_native = true;
        } else if (pfx(a, "--mx-include=")) { o->mx_include_dir = a + 13;
        } else if (eq(a, "--dry-run")) { o->dry_run = true;
        } else if (eq(a, "-E")) { o->mode = MODE_PREPROCESS;
        } else if (eq(a, "-S")) { o->mode = MODE_ASM;
        } else if (eq(a, "-c")) { o->mode = MODE_COMPILE;
        } else if (eq(a, "-o")) { NEEDARG(); o->out = argv[++i];
        } else if (pfx(a, "-o") && a[2]) { o->out = a + 2;
        } else if (pfx(a, "--emit-build-info=")) { o->build_info = a + 18;
        } else if (eq(a, "--keep-env")) { o->keep_env = true;
        } else if (is_denied_flag(a)) {
            die("'%s' is refused: it would make the compiler load or run code at build time (security review finding 3)", a);
        } else if (pfx(a, "-march=native") || pfx(a, "-mtune=native") || pfx(a, "-mcpu=native")) {
            if (!o->allow_native)
                die("'%s' is not reproducible; use --tier=baseline|avx2|avx512 (or --allow-native)", a);
            if (o->nextra >= MXCC_MAX_ARGS) die("too many arguments");
            o->extra[o->nextra++] = a;
        } else if (pfx(a, "-march=") || pfx(a, "-mcpu=")) {
            die("'%s' conflicts with --tier; tiers are the only supported ISA selection", a);
        } else if (eq(a, "-g") || pfx(a, "-g")) {
            o->debug = true;
            if (o->nextra >= MXCC_MAX_ARGS) die("too many arguments");
            o->extra[o->nextra++] = a;
        } else if (a[0] == '-' && a[1]) {
            if (o->nextra >= MXCC_MAX_ARGS) die("too many arguments");
            o->extra[o->nextra++] = a;
            /* options that take a separate argument: keep the pair together */
            if ((eq(a, "-I") || eq(a, "-D") || eq(a, "-U") || eq(a, "-include") || eq(a, "-isystem") ||
                 eq(a, "-imacros") || eq(a, "-L") || eq(a, "-l") || eq(a, "-x") || eq(a, "-MF") ||
                 eq(a, "-MT") || eq(a, "-MQ") || eq(a, "-Xlinker") || eq(a, "-Xclang") || eq(a, "-idirafter") ||
                 eq(a, "-iquote")) && i + 1 < argc) {
                if (o->nextra >= MXCC_MAX_ARGS) die("too many arguments");
                o->extra[o->nextra++] = argv[++i];
            }
        } else {
            if (o->ninputs >= MXCC_MAX_ARGS) die("too many input files");
            o->inputs[o->ninputs++] = a;
        }
        #undef NEEDARG
    }

    const char *ccb = base_name(o->cc);
    if (pfx(ccb, "clang")) o->cc_is_clang = true;
    else if (pfx(ccb, "gcc") || pfx(ccb, "cc") || strstr(ccb, "-gcc")) o->cc_is_clang = false;
    else die("--cc must name clang or gcc (got '%s')", o->cc);

    if (o->ninputs == 0) die("no input files");
    if (o->mode != MODE_LINK && o->ninputs > 1 && o->out)
        die("cannot specify -o with -c, -S or -E and multiple files");
    if (o->typed_input && !o->mx_include_dir) {
        /* default: <dir of mxcc>/../include, then ./include */
        static char buf[MXCC_MAX_PATH + 16];
        char exe[MXCC_MAX_PATH - 32];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0) {
            exe[n] = 0;
            char *slash = strrchr(exe, '/');
            if (slash) {
                *slash = 0;
                snprintf(buf, sizeof(buf), "%s/../include", exe);
                struct stat st;
                char probe[MXCC_MAX_PATH + 32];
                snprintf(probe, sizeof(probe), "%s/mx_input.h", buf);
                if (stat(probe, &st) == 0) o->mx_include_dir = buf;
                else {
                    snprintf(buf, sizeof(buf), "%s/include", exe);
                    snprintf(probe, sizeof(probe), "%s/mx_input.h", buf);
                    if (stat(probe, &st) == 0) o->mx_include_dir = buf;
                }
            }
        }
        if (!o->mx_include_dir) o->mx_include_dir = "include";
    }
}

/* ---- flag sets ------------------------------------------------------- */

static void add_profile(argv_t *a, const opts_t *o) {
    switch (o->profile) {
    case PROF_COMPILE_FAST:
        push(a, "-O0");
        push(a, "-fno-asynchronous-unwind-tables"); push(a, "-fno-unwind-tables");
        break;
    case PROF_MATRIXOS:
        push(a, "-O2");
        push(a, "-fno-asynchronous-unwind-tables"); push(a, "-fno-unwind-tables");
        push(a, "-fno-plt"); push(a, "-fomit-frame-pointer");
        push(a, "-ffunction-sections"); push(a, "-fdata-sections");
        push(a, "-fno-math-errno");
        break;
    case PROF_BALANCED:
        push(a, "-O1");
        push(a, "-fno-asynchronous-unwind-tables"); push(a, "-fno-unwind-tables");
        break;
    case PROF_RUNTIME_FAST:
        push(a, "-O3");
        push(a, "-fomit-frame-pointer"); push(a, "-fno-plt"); push(a, "-fno-math-errno");
        push(a, "-fno-semantic-interposition");
        break;
    }
    push(a, "-fno-ident");
}

static const char *tier_name(tier_t t) {
    return t == TIER_BASELINE ? "baseline" : t == TIER_AVX2 ? "avx2" : "avx512";
}

static void add_tier(argv_t *a, const opts_t *o) {
    if (o->arch == ARCH_X86_64) {
        /* psABI micro-architecture levels: fixed feature sets, identical on every build host */
        switch (o->tier) {
        case TIER_BASELINE: push(a, "-march=x86-64");    break;  /* SSE2 */
        case TIER_AVX2:     push(a, "-march=x86-64-v3"); break;  /* AVX, AVX2, FMA, BMI1/2, MOVBE, F16C, LZCNT */
        case TIER_AVX512:   push(a, "-march=x86-64-v4"); break;  /* v3 + AVX-512 F/BW/CD/DQ/VL */
        }
        push(a, "-mtune=generic");
    } else {
        if (o->tier != TIER_BASELINE) die("tiers avx2/avx512 are x86-64 only; aarch64 has baseline (armv8-a) only");
        push(a, "-march=armv8-a");
    }
    push(a, fmt_arg("-DMX_SIMD_TIER=%d", (int)o->tier));
    push(a, fmt_arg("-DMX_SIMD_TIER_NAME=\"%s\"", tier_name(o->tier)));
}

static void add_policy_warnings(argv_t *a, const opts_t *o) {
    push(a, "-Wall"); push(a, "-Wextra");
    push(a, "-Wformat=2"); push(a, "-Wimplicit-fallthrough");
    push(a, "-Wvla"); push(a, "-Walloca");            /* every buffer has a hard ceiling */
    push(a, "-Wshadow"); push(a, "-Wcast-qual"); push(a, "-Wpointer-arith");
    /* constraint violations that both compilers only warn about by default */
    push(a, "-Werror=implicit-function-declaration");
    push(a, "-Werror=implicit-int");
    push(a, "-Werror=int-conversion");
    push(a, "-Werror=incompatible-pointer-types");
    push(a, "-Werror=return-type");
    if (o->strict) {
        push(a, "-Werror=vla"); push(a, "-Werror=alloca");
        push(a, "-Wconversion"); push(a, "-Werror=conversion");
        push(a, "-Werror=cast-qual"); push(a, "-Werror=shadow");
    }
}

static void add_harden(argv_t *a, const opts_t *o) {
    push(a, "-fstack-protector-strong");
    push(a, "-fstack-clash-protection");
    push(a, "-ftrivial-auto-var-init=zero");
    push(a, "-fno-delete-null-pointer-checks");
    push(a, "-fno-strict-aliasing");
    if (o->arch == ARCH_X86_64) push(a, "-fcf-protection=full");
    else push(a, "-mbranch-protection=standard");
    if (!o->freestanding) {
        push(a, "-fPIE");
        if (o->profile != PROF_COMPILE_FAST) { push(a, "-U_FORTIFY_SOURCE"); push(a, "-D_FORTIFY_SOURCE=3"); }
    }
}

static void add_harden_link(argv_t *a, const opts_t *o) {
    push(a, "-Wl,-z,relro"); push(a, "-Wl,-z,now"); push(a, "-Wl,-z,noexecstack");
    if (!o->freestanding) push(a, "-pie");
}

static void add_reproducible(argv_t *a, const opts_t *o) {
    char cwd[MXCC_MAX_PATH];
    if (getcwd(cwd, sizeof(cwd))) {
        push(a, fmt_arg("-ffile-prefix-map=%s=.", cwd));
    }
    push(a, "-Wdate-time"); push(a, "-Werror=date-time");
    /* a stable seed per output name makes gcc's internal symbol naming deterministic */
    const char *seed = o->out ? base_name(o->out) : "mxcc";
    push(a, fmt_arg("-frandom-seed=%s", seed));
    if (o->cc_is_clang) push(a, "-fno-record-command-line");
    else push(a, "-fno-record-gcc-switches");
}

static void add_typed_input(argv_t *a, const opts_t *o) {
    push(a, "-DMX_TYPED_INPUT=1");
    push(a, fmt_arg("-I%s", o->mx_include_dir));
    push(a, "-include"); push(a, fmt_arg("%s/mx_input.h", o->mx_include_dir));
    /* the cell/control separation rests on these being errors, not warnings */
    push(a, "-Werror=int-conversion");
    push(a, "-Werror=incompatible-pointer-types");
    push(a, "-Werror=enum-conversion");
    push(a, "-Werror=pointer-sign");
    if (o->cc_is_clang) push(a, "-Werror=implicit-int-conversion");
    else push(a, "-Werror=int-in-bool-context");
}

static void compose(const opts_t *o, argv_t *a) {
    a->n = 0;
    push(a, o->cc_abs);
    push(a, "-pipe");
    push(a, "-std=gnu11");
    if (o->target) push(a, fmt_arg("--target=%s", o->target));
    if (o->sysroot) push(a, fmt_arg("--sysroot=%s", o->sysroot));
    if (o->freestanding) {
        push(a, "-ffreestanding"); push(a, "-nostdlib"); push(a, "-nostdinc");
        push(a, "-fno-builtin"); push(a, "-static");
    }
    push(a, fmt_arg("-DMX_PROFILE_%s=1",
        o->profile == PROF_COMPILE_FAST ? "COMPILE_FAST" : o->profile == PROF_MATRIXOS ? "MATRIXOS" :
        o->profile == PROF_BALANCED ? "BALANCED" : "RUNTIME_FAST"));
    add_profile(a, o);
    add_tier(a, o);
    add_policy_warnings(a, o);
    if (o->harden) add_harden(a, o);
    if (o->reproducible) add_reproducible(a, o);
    if (o->typed_input) add_typed_input(a, o);

    for (int i = 0; i < o->nextra; i++) push(a, o->extra[i]);

    switch (o->mode) {
    case MODE_PREPROCESS: push(a, "-E"); break;
    case MODE_ASM:        push(a, "-S"); break;
    case MODE_COMPILE:    push(a, "-c"); break;
    case MODE_LINK:
        if (o->harden) add_harden_link(a, o);
        if (o->profile == PROF_MATRIXOS && !o->debug) { push(a, "-Wl,--gc-sections"); push(a, "-s"); }
        break;
    }
    for (int i = 0; i < o->ninputs; i++) push(a, o->inputs[i]);
    if (o->out) { push(a, "-o"); push(a, o->out); }
    a->v[a->n] = NULL;
}

int main(int argc, char **argv) {
    opts_t o;
    argv_t a;
    parse_args(argc, argv, &o);
    if (o.dry_run && o.cc[0] != '/' && !strchr(o.cc, '/')) {
        /* dry runs must work without a compiler installed: print the name as given */
        snprintf(o.cc_abs, sizeof o.cc_abs, "%s", o.cc);
    } else resolve_compiler(&o);
    compose(&o, &a);
    if (o.build_info) emit_build_info(&o, &a);

    if (o.dry_run) {
        for (int i = 0; i < a.n; i++) { fputs(a.v[i], stdout); fputc('\n', stdout); }
        return 0;
    }
    /* keep the policy honest for anything the compiler itself expands from the environment */
    if (o.reproducible && !getenv("SOURCE_DATE_EPOCH")) setenv("SOURCE_DATE_EPOCH", "0", 0);
    if (o.keep_env) {
        extern char **environ;
        execve(a.v[0], (char *const *)a.v, environ);
    } else {
        /* minimal environment: the compiler finds cc1/as/ld through its own
         * install prefix plus this fixed PATH; no LD_PRELOAD, COMPILER_PATH,
         * GCC_EXEC_PREFIX, CPATH or similar can reach it from the caller. */
        const char *env[8]; int n = 0;
        env[n++] = "PATH=/usr/bin:/bin";
        env[n++] = "LC_ALL=C";
        static char sde[64], tmp[MXCC_MAX_PATH];
        snprintf(sde, sizeof sde, "SOURCE_DATE_EPOCH=%s", getenv("SOURCE_DATE_EPOCH") ? getenv("SOURCE_DATE_EPOCH") : "0");
        env[n++] = sde;
        if (getenv("TMPDIR")) { snprintf(tmp, sizeof tmp, "TMPDIR=%s", getenv("TMPDIR")); env[n++] = tmp; }
        env[n] = NULL;
        execve(a.v[0], (char *const *)a.v, (char *const *)env);
    }
    fprintf(stderr, "mxcc: error: cannot exec '%s': %s\n", a.v[0], strerror(errno));
    return 127;
}
