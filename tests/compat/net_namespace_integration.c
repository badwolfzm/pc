/* Host integration regression for Warden's user-space socket service.
 * cc -std=c11 -O0 -fno-builtin -ffunction-sections -fdata-sections \
 *    -Wl,--gc-sections -Wno-unused-function \
 *    tests/compat/net_namespace_integration.c -o /tmp/warden-netns-integration
 * /tmp/warden-netns-integration
 */
#define main warden_personality_guest_main
#include "../../crates/warden-kernel/user/personality.c"
#undef main
#include <assert.h>
#include <stdio.h>

static uint8_t bufs[7][BUF_SIZE];

static void init_client(int badge) {
    clients[badge].perms = manifest[badge].perms;
    clients[badge].netns = manifest[badge].netns;
    clients[badge].buf = bufs[badge];
}
static void sockaddr_port(int badge, unsigned short port) {
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(bufs[badge] + BUF_DATA_OFF, &sa, sizeof(sa));
}
static int make_socket(int badge) {
    long fd = do_socket(&clients[badge], AF_INET, SOCK_STREAM);
    assert(fd >= 0);
    return (int)fd;
}
int main(void) {
    for (int i = 1; i <= 6; i++) init_client(i);
    int native_srv = make_socket(1);
    sockaddr_port(1, 8080);
    assert(do_bind(&clients[1], native_srv, sizeof(struct sockaddr_in)) == 0);
    assert(do_listen(&clients[1], native_srv) == 0);
    int linux_srv = make_socket(4);
    sockaddr_port(4, 8080);
    assert(do_bind(&clients[4], linux_srv, sizeof(struct sockaddr_in)) == 0);
    assert(do_listen(&clients[4], linux_srv) == 0);
    puts("PASS same-port native and Linux listeners");

    int native_cli = make_socket(2);
    sockaddr_port(2, 8080);
    assert(do_connect(&clients[2], native_cli, sizeof(struct sockaddr_in)) == 0);
    assert(streams[streams[clients[2].fds[native_cli].idx].peer].owner == 1);
    puts("PASS native->native connection");

    int linux_cli = make_socket(5);
    sockaddr_port(5, 8080);
    assert(do_connect(&clients[5], linux_cli, sizeof(struct sockaddr_in)) == 0);
    assert(streams[streams[clients[5].fds[linux_cli].idx].peer].owner == 4);
    puts("PASS Linux->Linux connection");

    assert(do_close(&clients[1], native_srv) == 0);
    int native_cli2 = make_socket(2);
    sockaddr_port(2, 8080);
    assert(do_connect(&clients[2], native_cli2, sizeof(struct sockaddr_in)) == -ECONNREFUSED);
    puts("PASS native blocked from Linux-only listener");

    int native_srv2 = make_socket(1);
    sockaddr_port(1, 8080);
    assert(do_bind(&clients[1], native_srv2, sizeof(struct sockaddr_in)) == 0);
    assert(do_listen(&clients[1], native_srv2) == 0);
    assert(do_close(&clients[4], linux_srv) == 0);
    int linux_cli2 = make_socket(5);
    sockaddr_port(5, 8080);
    assert(do_connect(&clients[5], linux_cli2, sizeof(struct sockaddr_in)) == -ECONNREFUSED);
    puts("PASS Linux blocked from native-only listener");
    return 0;
}
