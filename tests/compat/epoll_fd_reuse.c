/* Host-level epoll descriptor-reuse regression for issue #52.
 * Compiles the real personality.c service into a test translation unit.
 * Not a QEMU guest test or proof of complete Linux epoll semantics.
 *
 * cc -std=c11 -O0 -fno-builtin -ffunction-sections -fdata-sections \
 *    -Wl,--gc-sections -Wno-unused-function \
 *    tests/compat/epoll_fd_reuse.c -o /tmp/warden-epoll-reuse
 * /tmp/warden-epoll-reuse
 */
#define main personality_service_entry
#include "../../crates/warden-kernel/user/personality.c"
#undef main
#include <assert.h>
#include <stdio.h>

static uint8_t testbuf[BUF_SIZE];

static void set_event(uint32_t events, uint64_t data) {
    struct epoll_event ev = {events, data};
    memcpy(testbuf + BUF_DATA_OFF, &ev, sizeof ev);
}

static void check_no_events(struct client *c, int epfd) {
    assert(do_epoll_wait(c, epfd, 8, 0) == 0);
}

int main(void) {
    struct client *c = &clients[1];
    c->perms = manifest[1].perms;
    c->netns = manifest[1].netns;
    c->buf = testbuf;
    int epfd = (int)do_epoll_create(c);
    assert(epfd >= 3);

    /* Closing the watched stream must remove its registration. */
    int old = (int)do_socket(c, AF_INET, SOCK_STREAM);
    assert(old >= 3);
    set_event(EPOLLOUT, UINT64_C(0x12345678));
    assert(do_epoll_ctl(c, epfd, EPOLL_CTL_ADD, old) == 0);
    assert(do_epoll_wait(c, epfd, 8, 0) == 1);
    assert(do_close(c, old) == 0);
    int replacement = (int)do_socket(c, AF_INET, SOCK_STREAM);
    assert(replacement == old);
    check_no_events(c, epfd);

    /* A deliberate new registration works, with only its new token. */
    set_event(EPOLLOUT, UINT64_C(0xABCDEF98));
    assert(do_epoll_ctl(c, epfd, EPOLL_CTL_ADD, replacement) == 0);
    assert(do_epoll_wait(c, epfd, 8, 0) == 1);
    struct epoll_event got;
    memcpy(&got, testbuf + BUF_DATA_OFF, sizeof got);
    assert(got.data == UINT64_C(0xABCDEF98));
    assert(do_close(c, replacement) == 0);
    check_no_events(c, epfd);

    /* Bound the cleanup: 100 close/reopen cycles cannot inherit watches. */
    for (int i = 0; i < 100; i++) {
        int fd = (int)do_socket(c, AF_INET, SOCK_STREAM);
        assert(fd >= 0);
        set_event(EPOLLOUT, (uint64_t)i + 1);
        assert(do_epoll_ctl(c, epfd, EPOLL_CTL_ADD, fd) == 0);
        assert(do_close(c, fd) == 0);
        check_no_events(c, epfd);
    }
    assert(epolls[c->fds[epfd].idx].n == 0);

    /* A close must remove the same watched fd from ALL epoll objects that
     * belong to the caller, without touching another caller's watch. */
    int epfd2 = (int)do_epoll_create(c);
    assert(epfd2 >= 0);
    int shared = (int)do_socket(c, AF_INET, SOCK_STREAM);
    assert(shared >= 0);
    set_event(EPOLLOUT, UINT64_C(0xDEAD));
    assert(do_epoll_ctl(c, epfd, EPOLL_CTL_ADD, shared) == 0);
    set_event(EPOLLOUT, UINT64_C(0xBEEF));
    assert(do_epoll_ctl(c, epfd2, EPOLL_CTL_ADD, shared) == 0);

    struct client *other = &clients[4];
    static uint8_t otherbuf[BUF_SIZE];
    other->perms = manifest[4].perms;
    other->netns = manifest[4].netns;
    other->buf = otherbuf;
    int other_epfd = (int)do_epoll_create(other);
    int other_stream = (int)do_socket(other, AF_INET, SOCK_STREAM);
    assert(other_epfd >= 0 && other_stream >= 0);
    struct epoll_event other_ev = {EPOLLOUT, UINT64_C(0xFACE)};
    memcpy(otherbuf + BUF_DATA_OFF, &other_ev, sizeof other_ev);
    assert(do_epoll_ctl(other, other_epfd, EPOLL_CTL_ADD, other_stream) == 0);
    assert(do_close(c, shared) == 0);
    check_no_events(c, epfd);
    check_no_events(c, epfd2);
    assert(do_epoll_wait(other, other_epfd, 8, 0) == 1);
    struct epoll_event other_got;
    memcpy(&other_got, otherbuf + BUF_DATA_OFF, sizeof other_got);
    assert(other_got.data == UINT64_C(0xFACE));
    assert(do_close(other, other_stream) == 0);
    assert(do_close(other, other_epfd) == 0);
    assert(do_close(c, epfd2) == 0);
    assert(do_close(c, epfd) == 0);
    puts("PASS: epoll cleanup prevents stale watcher reuse");
    return 0;
}
