/*
 * mx_fuzz_compat.h -- replacement for the baseline mx_fuzz_run (security review
 * finding 5). The baseline generator never produced a path starting with
 * "/sandbox/", so every iteration was rejected at the prefix check and the
 * jail, the readers, the MXSB sniffer and the canvas were never exercised.
 *
 * Swap-in (harness): wrap the baseline block (typedef mx_fuzz_stats ..
 * mx_fuzz_run) in #ifndef MX_FUZZ_COMPAT and include this header before it.
 * Same names and the same mx_fuzz_stats fields the tests and benches read,
 * plus: paths_accepted, reads_ok, writes_ok, escapes, invariant_fails.
 *
 * Crash detection is NOT in-process: a crash is caught by running the harness
 * under -fsanitize=address,undefined (security_regressions.sh F5 does). The
 * `crashes` field counts in-process invariant violations instead:
 *   - a read or write through a symlink out of the sandbox succeeded (escape)
 *   - the type sniffer returned a kind outside its enum
 *   - the canvas changed_count parity disagrees with the shadow map parity
 * The fuzzer uses its own oracle so the main deterministic trace hash is
 * unchanged (the golden pin on it still holds).
 */
#ifndef MX_FUZZ_COMPAT_H
#define MX_FUZZ_COMPAT_H
#define MX_FUZZ_COMPAT 1

static void mx_ensure_dir(const char *path);          /* defined later in the baseline */
static void mx_write_text_file(const char *path, const char *s);

typedef struct {
    uint32_t iterations, vfs_rejects, superop_rejects, lift_detects, crashes; uint64_t acc;
    uint32_t paths_accepted, reads_ok, writes_ok, escapes, invariant_fails, sniff_hits[5];
} mx_fuzz_stats;

static void mx_rand_fill(uint64_t *s, uint8_t *p, size_t n) { for (size_t i = 0; i < n; ++i) p[i] = (uint8_t)mx_xorshift64(s); }

static const char *const mx_fz_seg[] = { "hello.txt", "fz_link_out", "fz_dir", "fz_dir/x.bin", "..", ".", "fz_dir/..", "a", "very_long_name_0123456789_0123456789", "fz_w.bin", "nonexistent", "", "fz_link_in" };
#define MX_FZ_NSEG (sizeof mx_fz_seg / sizeof mx_fz_seg[0])

static void mx_fz_setup_tree(void) {
    mx_ensure_dir("sandbox"); mx_ensure_dir("sandbox/fz_dir");
    mx_write_text_file("sandbox/fz_dir/x.bin", "x");
    mx_write_text_file("fz_outside.txt", "OUTSIDE");
#if !defined(_WIN32)
    unlink("sandbox/fz_link_out"); (void)symlink("../fz_outside.txt", "sandbox/fz_link_out");
    unlink("sandbox/fz_link_in");  (void)symlink("hello.txt", "sandbox/fz_link_in");
#endif
}

static void mx_fz_path(uint64_t *seed, char out[128]) {
    uint8_t r[8]; mx_rand_fill(seed, r, sizeof r);
    size_t n = 0; out[0] = 0;
    switch (r[0] % 8) {
    case 0: n = (size_t)snprintf(out, 128, "/etc/%s", mx_fz_seg[r[1] % MX_FZ_NSEG]); break;          /* wrong prefix */
    case 1: n = (size_t)snprintf(out, 128, "/sandbox%s", r[1] & 1 ? "x/a" : "");  break;             /* prefix without the slash */
    case 2: { n = (size_t)snprintf(out, 128, "/sandbox/"); size_t L = 9 + (r[1] % 110); for (; n < L && n < 127; ++n) out[n] = "abc./_\\"[r[(n) % 8] % 7]; out[n] = 0; break; } /* random bytes */
    default: { n = (size_t)snprintf(out, 128, "/sandbox/%s", mx_fz_seg[r[1] % MX_FZ_NSEG]); int depth = r[2] % 3; for (int d = 0; d < depth && n < 100; ++d) n += (size_t)snprintf(out + n, 128 - n, "/%s", mx_fz_seg[r[3 + d] % MX_FZ_NSEG]); break; }
    }
    (void)n;
}

static void mx_fuzz_run(mx_oracle *o_unused, mx_fuzz_stats *fs, uint32_t iters) {
    (void)o_unused; memset(fs, 0, sizeof *fs); fs->iterations = iters;
    mx_oracle o; mx_oracle_bootstrap(&o, MX_CAP_FS_READ | MX_CAP_FS_WRITE | MX_CAP_SUPEROP | MX_CAP_GPU, true); /* own oracle: keeps the main trace hash stable */
    mx_fz_setup_tree();
    mx_vfs vfs; (void)mx_vfs_init(&vfs, &o, "./sandbox", true);
    uint64_t seed = 0xF00DCAFE12345678ULL; uint8_t scratch[256];
    mx_canvas cv; int have_cv = mx_canvas_init(&cv, 37, 11) == 0; /* odd width: shadow-map bit packing */
    for (uint32_t i = 0; i < iters; ++i) {
        mx_rand_fill(&seed, scratch, sizeof scratch);
        /* --- VFS jail --- */
        char guest[128]; mx_fz_path(&seed, guest); char host[MX_MAX_PATH];
        int prc = mx_vfs_map_guest_to_host(&vfs, guest, host);
        if (prc != 0) fs->vfs_rejects++; else fs->paths_accepted++;
        int via_link_out = strcmp(guest, "/sandbox/fz_link_out") == 0 || strcmp(guest, "/sandbox/fz_dir/../fz_link_out") == 0 || strcmp(guest, "/sandbox/./fz_link_out") == 0; /* the planted symlink itself */
        if (prc == 0 && (scratch[0] & 1)) { uint8_t *buf = NULL; size_t n = 0; int rrc = mx_vfs_read_all(&vfs, guest, &buf, &n);
            if (rrc == 0) { fs->reads_ok++; if (via_link_out || (buf && n >= 7 && memcmp(buf, "OUTSIDE", 7) == 0)) { fs->escapes++; fs->crashes++; } fs->acc ^= mx_fnv1a64(buf, n); free(buf); } }
        if (prc == 0 && (scratch[0] & 2) && strstr(guest, "fz_w.bin")) { int wrc = mx_vfs_write_all(&vfs, guest, scratch, 16); if (wrc == 0) fs->writes_ok++; }
        if (prc == 0 && (scratch[0] & 2) && via_link_out) { int wrc = mx_vfs_write_all(&vfs, guest, "PWNED", 5); if (wrc == 0) { fs->escapes++; fs->crashes++; } }
        /* --- super-ops with correctly sized buffers (the contract's lengths are the fuzz target) --- */
        uint64_t ret = 0; size_t a1 = scratch[1] % 40, a3 = scratch[2] % 40; int id = scratch[3] % 8;
#ifdef MX_SUPEROPS_COMPAT_H
        int src = mx_superop_dispatch_b(&o, id, scratch + 64, 32, a1, scratch + 128, 32, a3, &ret);
#else
        int src = mx_superop_dispatch(&o, (mx_superop_id)id, scratch + 64, a1 <= 32 ? a1 : 32, scratch + 128, a3 <= 32 ? a3 : 32, &ret);
#endif
        if (src != 0) fs->superop_rejects++;
        /* --- type sniffer on random and near-valid headers --- */
        uint8_t hdr[32]; memcpy(hdr, scratch + 160, 32); if (scratch[4] & 1) memcpy(hdr, (scratch[4] & 2) ? "MXSB" : "\177ELF", 4);
        mx_guest_kind k = mx_sniff_guest(hdr, scratch[5] % 33); if ((unsigned)k > 4u) { fs->invariant_fails++; fs->crashes++; } else fs->sniff_hits[k]++;
        /* --- canvas: in- and out-of-bounds primitives, then the shadow-map parity invariant --- */
        if (have_cv) { int x = (int)(int8_t)scratch[6], y = (int)(int8_t)scratch[7]; uint32_t col = *(uint32_t *)(scratch + 8) & 0x00FFFFFFu;
            switch (scratch[12] % 3) { case 0: mx_canvas_pset(&cv, x, y, col); break; case 1: mx_canvas_rect(&cv, x, y, scratch[13] % 50 - 5, scratch[14] % 20 - 3, col); break; default: mx_canvas_line(&cv, x, y, (int)(int8_t)scratch[15], (int)(int8_t)scratch[16], col); break; }
            uint64_t pop = 0; for (size_t b = 0; b < mx_canvas_shadow_bytes(cv.w, cv.h); ++b) pop += (uint64_t)__builtin_popcount(cv.xor_shadow_1b[b]);
            if ((pop & 1u) != (cv.changed_count & 1u)) { fs->invariant_fails++; fs->crashes++; } }
        /* --- loop detector (legacy flags shape via the lifter compat header or baseline) --- */
        mx_loop_sig sig = { .tag = "fuzz", .ops_mask = scratch[17], .has_shift_right1 = !!(scratch[18] & 1), .has_xor = !!(scratch[19] & 1), .has_and_lowbit = !!(scratch[20] & 1), .has_const_poly = !!(scratch[21] & 1), .has_const_affine = !!(scratch[22] & 1) };
        mx_loop_kind lk = mx_detect_liftable_loop(&sig); if (lk == MX_LOOP_LFSR32_AFFINE) fs->lift_detects++;
        fs->acc ^= ret + ((uint64_t)lk << 32) + (uint64_t)(uint8_t)host[0] + ((uint64_t)prc << 40);
    }
    { /* the outside file must still hold its original content: any write through the symlink is an escape */
      FILE *f = fopen("fz_outside.txt", "rb"); char b[16] = {0}; if (f) { size_t n = fread(b, 1, 15, f); fclose(f); if (n != 7 || memcmp(b, "OUTSIDE", 7) != 0) { fs->escapes++; fs->crashes++; } } }
    if (have_cv) mx_canvas_free(&cv);
#ifdef MX_VFS_COMPAT
    mx_vfs_destroy(&vfs);
#endif
}
#endif
