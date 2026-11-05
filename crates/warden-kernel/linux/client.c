/* An HTTP client, as a plain Linux program. It holds net.connect and no
 * fs.read, so its own openat must fail. It also exercises the memory calls
 * the kernel serves itself: brk, mmap, munmap, uname. */
#include "sys.h"

static i64 get(const char *path, int *status, i64 *body) {
    int s = (int)sys3(SYS_socket, 2, 1, 0);
    if (s < 0) return s;
    struct sockaddr_in sa = { 2, htons(8081), 0x0100007f, {0} };
    i64 r = -111;
    for (int tries = 0; tries < 300 && r == -111; tries++) {
        r = sys3(SYS_connect, s, &sa, sizeof sa);
        if (r == -111) sleep_ms(10);
    }
    if (r < 0) { sys1(SYS_close, s); return r; }
    char req[128]; int n = 0;
    const char *a = "GET "; while (*a) req[n++] = *a++;
    while (*path) req[n++] = *path++;
    a = " HTTP/1.0\r\nHost: warden\r\n\r\n"; while (*a) req[n++] = *a++;
    sys3(SYS_write, s, req, n);
    char buf[2048]; i64 got = 0;
    for (;;) {
        i64 k = sys3(SYS_read, s, buf + got, sizeof buf - 1 - got);
        if (k == -11) continue;
        if (k <= 0) break;
        got += k;
        if (got >= (i64)sizeof buf - 1) break;
    }
    buf[got] = 0;
    sys1(SYS_close, s);
    *status = 0;
    if (starts(buf, "HTTP/1.0 ")) *status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
    *body = 0;
    for (i64 i = 0; i + 3 < got; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') { *body = got - (i + 4); break; }
    return 0;
}

void prog(void) {
    int st = 0; i64 body = 0;
    i64 r = get("/index.html", &st, &body);
    check(142, r == 0 ? st : r);
    check(143, body);
    r = get("/missing", &st, &body);
    check(144, r == 0 ? st : r);
    check(145, sys3(SYS_openat, -100, "/site/index.html", 0));
    char un[65 * 6]; sys1(SYS_uname, un);
    check(146, starts(un, "Linux") && starts(un + 65 * 4, "x86_64"));
    u64 brk0 = (u64)sys1(SYS_brk, 0);
    u64 brk1 = (u64)sys1(SYS_brk, brk0 + 3 * 4096);
    volatile u64 *heap = (u64 *)brk0;
    heap[0] = 0x1234; heap[1535] = 0x5678;
    check(147, brk1 == brk0 + 3 * 4096 && heap[0] == 0x1234 && heap[1535] == 0x5678);
    i64 m = sys6(SYS_mmap, 0, 2 * 4096, 3, 0x22, -1, 0);
    volatile u64 *mm = (u64 *)m;
    if (m > 0) { mm[0] = 0xABC; mm[1023] = 0xDEF; }
    check(148, m > 0 && mm[0] == 0xABC && mm[1023] == 0xDEF);
    i64 u = sys2(SYS_munmap, m, 2 * 4096);
    i64 m2 = sys6(SYS_mmap, 0, 2 * 4096, 3, 0x22, -1, 0);
    volatile u64 *mm2 = (u64 *)m2;
    /* a fresh anonymous mapping is zero even though the old one held data */
    check(149, u == 0 && m2 == m && mm2[0] == 0 && mm2[1023] == 0);
    /* ABI fidelity: output arguments must be filled, not just return 0. */
    struct { i64 sec, usec; } tv = {-1, -1};
    check(164, sys2(SYS_gettimeofday, &tv, 0) == 0 && tv.sec >= 0 &&
          tv.usec >= 0 && tv.usec < 1000000);
    struct { u64 soft, hard; } lim = {0, 0};
    check(165, sys2(SYS_getrlimit, 7, &lim) == 0 && lim.soft == 16 && lim.hard == 16);
    struct { u64 soft, hard; } oldlim = {0, 0};
    check(166, sys4(SYS_prlimit64, 0, 3, 0, &oldlim) == 0 &&
          oldlim.soft == 65536 && oldlim.hard == 65536);
    u64 cpumask = 0;
    check(167, sys3(SYS_sched_getaffinity, 0, sizeof(cpumask), &cpumask) == 8 && cpumask == 1);
    struct timespec resolution = {-1, -1};
    check(168, sys2(SYS_clock_getres, 1, &resolution) == 0 &&
          resolution.sec == 0 && resolution.nsec == 10000000);
    char entropy[32];
    check(169, sys3(SYS_getrandom, entropy, sizeof(entropy), 0) == -38);
    exit_group(0);
}
